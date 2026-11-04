#include "core/reader/line_reader.h"

#include "core/util/assert.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace lv::core
{

namespace
{

tl::unexpected<Error> changedError(const File& file)
{
    return makeError(ErrorCode::kOutOfRange,
                     "file '" + file.path().string() + "' is shorter than its index; it changed");
}

// One contiguous byte range of the file that covers one or more lines.
struct Window
{
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    std::size_t firstLine = 0;  // index into the per-line ranges
};

}  // namespace

LineReader::LineReader(const File& file, const LineIndex& index, LineReaderOptions options)
    : m_file(&file)
    , m_index(&index)
    , m_options(options)
{
    LV_ASSERT(m_options.pageSize > 0 && m_options.cachePages > 0 && m_options.maxLineBytes > 0);
}

void LineReader::clearCache()
{
    const std::scoped_lock lock(m_cacheMutex);
    m_pages.clear();
    m_lru.clear();
}

Result<void> LineReader::readDirect(std::uint64_t offset, std::span<std::byte> out) const
{
    m_readCalls.fetch_add(1, std::memory_order_relaxed);
    const auto read = m_file->readAt(ByteOffset(offset), out);
    if (!read) {
        return tl::unexpected(read.error());
    }
    if (*read < out.size()) {
        return changedError(*m_file);
    }
    return {};
}

Result<void> LineReader::readRange(std::uint64_t offset, std::span<std::byte> out) const
{
    if (out.empty()) {
        return {};
    }
    const std::uint64_t pageSize = m_options.pageSize;
    // Ranges larger than half the cache bypass it, so one long line does not evict everything.
    if (out.size() > (m_options.cachePages * pageSize) / 2) {
        return readDirect(offset, out);
    }
    const std::uint64_t end = offset + out.size();
    const std::uint64_t firstPage = offset / pageSize;
    const std::uint64_t lastPage = (end - 1) / pageSize;

    const std::scoped_lock lock(m_cacheMutex);
    // A cached page is usable if it holds all bytes this request needs from it.
    auto needed = [&](std::uint64_t page) {
        return std::min(end, (page + 1) * pageSize) - (page * pageSize);
    };
    auto usable = [&](std::uint64_t page) {
        const auto found = m_pages.find(page);
        return found != m_pages.end() && found->second.data.size() >= needed(page);
    };

    // Read missing pages in contiguous runs, one readAt call per run.
    std::uint64_t page = firstPage;
    while (page <= lastPage) {
        if (usable(page)) {
            ++page;
            continue;
        }
        std::uint64_t runEnd = page;
        while (runEnd + 1 <= lastPage && !usable(runEnd + 1)) {
            ++runEnd;
        }
        std::vector<std::byte> buffer(static_cast<std::size_t>((runEnd - page + 1) * pageSize));
        m_readCalls.fetch_add(1, std::memory_order_relaxed);
        const auto read = m_file->readAt(ByteOffset(page * pageSize), buffer);
        if (!read) {
            return tl::unexpected(read.error());
        }
        if (*read < needed(runEnd) + ((runEnd - page) * pageSize)) {
            return changedError(*m_file);
        }
        for (std::uint64_t p = page; p <= runEnd; ++p) {
            const auto from = static_cast<std::size_t>((p - page) * pageSize);
            const std::size_t size = std::min<std::size_t>(static_cast<std::size_t>(pageSize),
                                                           *read - std::min(*read, from));
            const auto [found, inserted] = m_pages.try_emplace(p);
            Page& entry = found->second;
            if (inserted) {
                m_lru.push_front(p);
                entry.lru = m_lru.begin();
            }
            entry.data.assign(buffer.begin() + static_cast<std::ptrdiff_t>(from),
                              buffer.begin() + static_cast<std::ptrdiff_t>(from + size));
        }
        page = runEnd + 1;
    }

    // Copy out and mark the pages as recently used.
    for (page = firstPage; page <= lastPage; ++page) {
        auto& entry = m_pages.at(page);
        m_lru.splice(m_lru.begin(), m_lru, entry.lru);
        const std::uint64_t pageBegin = page * pageSize;
        const std::uint64_t from = std::max(offset, pageBegin);
        const std::uint64_t to = std::min(end, pageBegin + pageSize);
        std::memcpy(out.data() + (from - offset), entry.data.data() + (from - pageBegin),
                    static_cast<std::size_t>(to - from));
    }

    // Evict least recently used pages beyond the capacity (never the ones just used).
    while (m_pages.size() > std::max<std::size_t>(m_options.cachePages, lastPage - firstPage + 1)) {
        m_pages.erase(m_lru.back());
        m_lru.pop_back();
    }
    return {};
}

Result<LineBlock> LineReader::readLines(const LineIndex::Snapshot& snapshot, LineNumber first,
                                        LineCount count) const
{
    LV_ASSERT(first.get() + count.get() <= snapshot.lineCount().get());
    const auto& traits = lv::core::traits(m_index->encoding());
    const std::size_t unit = traits.codeUnitSize;

    LineBlock block;
    block.first = first;
    const auto lineCount = static_cast<std::size_t>(count.get());
    block.lines.resize(lineCount);

    // Per-line raw ranges, capped at the maximum line length.
    std::vector<ByteRange> ranges(lineCount);
    for (std::size_t i = 0; i < lineCount; ++i) {
        ByteRange range = m_index->rawLineRange(snapshot, first + LineCount(i));
        if (range.size() > m_options.maxLineBytes) {
            const std::uint64_t cut = m_options.maxLineBytes - (m_options.maxLineBytes % unit);
            range = ByteRange::fromSize(range.begin(), cut);
            block.lines[i].truncated = true;
        }
        ranges[i] = range;
    }

    // Group lines into windows of contiguous file data (only line terminators in between).
    std::vector<Window> windows;
    for (std::size_t i = 0; i < lineCount; ++i) {
        const ByteRange& range = ranges[i];
        if (!windows.empty() && !block.lines[i - 1].truncated &&
            range.begin().get() <= windows.back().end + (2 * unit)) {
            windows.back().end = range.end().get();
            continue;
        }
        windows.push_back({.begin = range.begin().get(), .end = range.end().get(), .firstLine = i});
    }

    std::size_t total = 0;
    for (const Window& window : windows) {
        total += static_cast<std::size_t>(window.end - window.begin);
    }
    block.bytes.resize(total);

    std::size_t position = 0;
    for (std::size_t w = 0; w < windows.size(); ++w) {
        const Window& window = windows[w];
        const auto size = static_cast<std::size_t>(window.end - window.begin);
        if (auto read = readRange(window.begin, std::span(block.bytes).subspan(position, size));
            !read) {
            return tl::unexpected(read.error());
        }
        const std::size_t lastLine = w + 1 < windows.size() ? windows[w + 1].firstLine : lineCount;
        for (std::size_t i = window.firstLine; i < lastLine; ++i) {
            auto& line = block.lines[i];
            line.offset =
                position + static_cast<std::size_t>(ranges[i].begin().get() - window.begin);
            line.length = static_cast<std::size_t>(ranges[i].size());
        }
        position += size;
    }

    // Drop a carriage return before the line feed (complete, untruncated lines only).
    const std::uint64_t completeLines = snapshot.completeLines.get();
    for (std::size_t i = 0; i < lineCount; ++i) {
        auto& line = block.lines[i];
        if (line.truncated || first.get() + i >= completeLines || line.length < unit) {
            continue;
        }
        const std::byte* tail = block.bytes.data() + line.offset + line.length - unit;
        if (std::memcmp(tail, traits.carriageReturn.data(), unit) == 0) {
            line.length -= unit;
        }
    }
    return block;
}

}  // namespace lv::core
