#include "core/search/search_engine.h"

#include "core/concurrency/thread_pool.h"
#include "core/encoding/decode.h"
#include "core/encoding/encoding.h"
#include "core/index/sorted_u64_sequence.h"
#include "core/platform/file.h"
#include "core/search/matcher.h"
#include "core/util/assert.h"

#include <simdutf.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lv::core
{

SequenceSink::SequenceSink(SortedU64Sequence& matches)
    : m_matches(&matches)
{}

void SequenceSink::append(std::span<const std::uint64_t> lines, const SearchProgress& progress)
{
    m_matches->append(lines);
    m_matches->publish();
    const std::scoped_lock lock(m_mutex);
    m_progress = progress;
}

SearchProgress SequenceSink::progress() const
{
    const std::scoped_lock lock(m_mutex);
    return m_progress;
}

namespace
{

// Lines are checked for cancellation this often.
constexpr std::uint64_t kStopCheckInterval = 1024;

struct ChunkJob
{
    std::uint64_t firstLine = 0;
    std::uint64_t endLine = 0;
    std::uint64_t beginByte = 0;
    std::uint64_t endByte = 0;
    // The last line is the pending tail (no line feed).
    bool endsWithTail = false;
};

struct ChunkResult
{
    ChunkJob job;
    std::vector<std::uint64_t> matches;
    std::uint64_t skipped = 0;
    std::optional<Error> error;
    bool canceled = false;
    bool done = false;
};

const char* asChars(const std::byte* data) noexcept
{
    return reinterpret_cast<const char*>(data);  // NOLINT(*-reinterpret-cast)
}

void appendUtf8(std::u16string_view text, std::string& out)
{
    out.clear();
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size()) {
            const std::uint32_t low = text[i + 1];
            c = 0x10000 + ((c - 0xD800) << 10U) + (low - 0xDC00);
            ++i;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6U));
            out += static_cast<char>(0x80 | (c & 0x3FU));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12U));
            out += static_cast<char>(0x80 | ((c >> 6U) & 0x3FU));
            out += static_cast<char>(0x80 | (c & 0x3FU));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18U));
            out += static_cast<char>(0x80 | ((c >> 12U) & 0x3FU));
            out += static_cast<char>(0x80 | ((c >> 6U) & 0x3FU));
            out += static_cast<char>(0x80 | (c & 0x3FU));
        }
    }
}

// Byte offset of the next line feed code unit at or after `from` (aligned to code units), or
// npos.
std::size_t findLineFeed(std::span<const std::byte> data, std::size_t from, Encoding encoding)
{
    const auto& lf = traits(encoding).lineFeed;
    if (lf.size() == 1) {
        if (from >= data.size()) {
            return std::string_view::npos;
        }
        const void* hit =
            std::memchr(data.data() + from, static_cast<int>(lf[0]), data.size() - from);
        return hit == nullptr
                   ? std::string_view::npos
                   : static_cast<std::size_t>(static_cast<const std::byte*>(hit) - data.data());
    }
    for (std::size_t i = from; i + 1 < data.size(); i += 2) {
        if (data[i] == lf[0] && data[i + 1] == lf[1]) {
            return i;
        }
    }
    return std::string_view::npos;
}

bool endsWithCarriageReturn(std::span<const std::byte> line, Encoding encoding)
{
    const auto& cr = traits(encoding).carriageReturn;
    return line.size() >= cr.size() &&
           std::equal(cr.begin(), cr.end(), line.end() - static_cast<std::ptrdiff_t>(cr.size()));
}

// True if every line of the chunk is valid UTF-8 as stored, so it can be matched in place.
bool searchableInPlace(std::span<const std::byte> data, Encoding encoding)
{
    switch (encoding) {
    case Encoding::kUtf8:
        return simdutf::validate_utf8(asChars(data.data()), data.size());
    case Encoding::kWindows1252:
    case Encoding::kIso88591:
        return simdutf::validate_ascii(asChars(data.data()), data.size());
    case Encoding::kUtf16Le:
    case Encoding::kUtf16Be:
        return false;
    }
    return false;
}

void searchChunk(const SearchRequest& request, const std::stop_token& stop, ChunkResult& result)
{
    const ChunkJob& job = result.job;
    const auto size = static_cast<std::size_t>(job.endByte - job.beginByte);
    thread_local std::vector<std::byte> buffer;
    thread_local std::u16string wide;
    thread_local std::string converted;
    buffer.resize(size);
    const auto read = request.file->readAt(ByteOffset(job.beginByte), buffer);
    if (!read) {
        result.error = read.error();
        return;
    }
    if (*read < size) {
        result.error = Error{.code = ErrorCode::kOutOfRange,
                             .systemCode = 0,
                             .message = "the file is shorter than its index"};
        return;
    }
    const std::span<const std::byte> data(buffer);
    const Encoding encoding = request.index->encoding();
    const std::size_t unit = traits(encoding).codeUnitSize;
    const bool inPlace = searchableInPlace(data, encoding);
    MatchScratch scratch;
    std::size_t position = 0;
    for (std::uint64_t line = job.firstLine; line < job.endLine; ++line) {
        if ((line - job.firstLine) % kStopCheckInterval == 0 && stop.stop_requested()) {
            result.canceled = true;
            return;
        }
        const std::size_t lineFeed = findLineFeed(data, position, encoding);
        std::size_t end = 0;
        std::size_t next = 0;
        if (lineFeed == std::string_view::npos) {
            if (!job.endsWithTail || line + 1 != job.endLine) {
                result.error = Error{.code = ErrorCode::kOutOfRange,
                                     .systemCode = 0,
                                     .message = "the file changed while searching"};
                return;
            }
            end = size;
            next = size;
        } else {
            end = lineFeed;
            next = lineFeed + unit;
            if (endsWithCarriageReturn(data.subspan(position, end - position), encoding)) {
                end -= unit;
            }
        }
        const auto raw = data.subspan(position, end - position);
        std::string_view text(asChars(raw.data()), raw.size());
        if (!inPlace) {
            if (!searchableInPlace(raw, encoding)) {
                decodeLine(raw, encoding, wide);
                appendUtf8(wide, converted);
                text = converted;
            }
        }
        if (request.matcher->matches(text, scratch)) {
            result.matches.push_back(line);
        }
        position = next;
    }
    if (position != size) {
        result.error = Error{.code = ErrorCode::kOutOfRange,
                             .systemCode = 0,
                             .message = "the file changed while searching"};
        return;
    }
    result.skipped = scratch.skippedLines;
}

// Splits the range into chunks of about `chunkBytes` of whole lines.
class Chunker
{
public:
    Chunker(const SearchRequest& request, std::uint64_t first, std::uint64_t end)
        : m_request(&request)
        , m_next(first)
        , m_end(end)
        , m_unit(traits(request.index->encoding()).codeUnitSize)
        , m_endByte(lineEndByte(end - 1))
    {}

    [[nodiscard]] bool done() const noexcept { return m_next >= m_end; }

    ChunkJob next()
    {
        const auto& snapshot = m_request->snapshot;
        ChunkJob job;
        job.firstLine = m_next;
        job.beginByte = m_request->index->rawLineRange(snapshot, LineNumber(m_next)).begin().get();
        const std::uint64_t target = job.beginByte + m_request->options.chunkBytes;
        std::uint64_t endLine = m_end;
        if (target < m_endByte) {
            if (const auto line = m_request->index->lineContaining(snapshot, ByteOffset(target))) {
                endLine = std::clamp(line->get() + 1, m_next + 1, m_end);
            }
        }
        job.endLine = endLine;
        job.endByte = lineEndByte(endLine - 1);
        job.endsWithTail = endLine - 1 == snapshot.completeLines.get();
        m_next = endLine;
        return job;
    }

private:
    // End of a line including its line feed (the indexed end for the pending tail).
    [[nodiscard]] std::uint64_t lineEndByte(std::uint64_t line) const
    {
        const auto& snapshot = m_request->snapshot;
        if (line == snapshot.completeLines.get()) {
            return snapshot.indexedEnd.get();
        }
        return m_request->index->rawLineRange(snapshot, LineNumber(line)).end().get() + m_unit;
    }

    const SearchRequest* m_request;
    std::uint64_t m_next;
    std::uint64_t m_end;
    std::size_t m_unit;
    std::uint64_t m_endByte;
};

}  // namespace

Result<SearchStats> SearchEngine::run(const SearchRequest& request, ThreadPool& pool,
                                      const std::stop_token& stop, ResultSink& sink)
{
    LV_ASSERT(request.file != nullptr && request.index != nullptr && request.matcher != nullptr);
    LV_ASSERT(request.options.chunkBytes > 0);
    const LineCount total = request.snapshot.lineCount();
    const SearchRange range =
        request.range.value_or(SearchRange{.first = LineNumber(0), .count = total});
    LV_ASSERT(range.first.get() + range.count.get() <= total.get());
    SearchStats stats;
    if (range.count.get() == 0) {
        return stats;
    }
    const std::uint64_t first = range.first.get();
    const std::uint64_t end = first + range.count.get();
    const std::size_t maxInFlight = request.options.maxChunksInFlight != 0
                                        ? request.options.maxChunksInFlight
                                        : 2 * pool.size();

    std::stop_source cancel;
    const std::stop_callback onStop(stop, [&cancel] { cancel.request_stop(); });
    std::mutex mutex;
    std::condition_variable done;
    std::deque<std::shared_ptr<ChunkResult>> inFlight;  // submission order = line order
    Chunker chunker(request, first, end);
    std::optional<Error> failure;
    bool canceled = false;

    while (true) {
        while (!failure && !cancel.stop_requested() && !chunker.done() &&
               inFlight.size() < maxInFlight) {
            auto slot = std::make_shared<ChunkResult>();
            slot->job = chunker.next();
            inFlight.push_back(slot);
            pool.submit(
                [&request, &mutex, &done, slot](const std::stop_token& token) {
                    ChunkResult local;
                    local.job = slot->job;
                    searchChunk(request, token, local);
                    const std::scoped_lock lock(mutex);
                    *slot = std::move(local);
                    slot->done = true;
                    // Notify under the lock: run() may return as soon as it sees `done`.
                    done.notify_all();
                },
                cancel.get_token());
        }
        if (inFlight.empty()) {
            break;
        }
        std::vector<std::shared_ptr<ChunkResult>> ready;
        {
            std::unique_lock lock(mutex);
            done.wait(lock, [&inFlight] { return inFlight.front()->done; });
            while (!inFlight.empty() && inFlight.front()->done) {
                ready.push_back(std::move(inFlight.front()));
                inFlight.pop_front();
            }
        }
        for (const auto& result : ready) {
            if (result->error && !failure) {
                failure = result->error;
                cancel.request_stop();
            }
            canceled = canceled || result->canceled;
            if (failure || canceled || cancel.stop_requested()) {
                continue;  // never emit after a gap: results stay an ordered prefix
            }
            stats.matches += result->matches.size();
            stats.skippedLines += result->skipped;
            stats.searchedLines = LineCount(stats.searchedLines.get() +
                                            (result->job.endLine - result->job.firstLine));
            ++stats.chunks;
            sink.append(result->matches, SearchProgress{.searchedLines = stats.searchedLines,
                                                        .totalLines = range.count,
                                                        .matches = stats.matches,
                                                        .skippedLines = stats.skippedLines});
        }
    }
    if (failure) {
        return tl::unexpected(*failure);
    }
    if (canceled || cancel.stop_requested()) {
        return makeError(ErrorCode::kCanceled, "search canceled");
    }
    return stats;
}

}  // namespace lv::core
