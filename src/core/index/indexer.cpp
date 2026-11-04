#include "core/index/indexer.h"

#include "core/index/line_splitter.h"
#include "core/util/assert.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace lv::core
{

namespace
{

struct Block
{
    std::vector<std::byte> data;
    std::size_t size = 0;
    std::uint64_t offset = 0;
    std::optional<Error> error;
};

// Ring of buffers shared by the reader thread and the scanning thread.
class BlockRing
{
public:
    BlockRing(std::size_t count, std::size_t blockSize)
        : m_blocks(count)
    {
        for (std::size_t i = 0; i < count; ++i) {
            m_blocks[i].data.resize(blockSize);
            m_free.push_back(i);
        }
    }

    // Reader: next free buffer, or nullopt when stopped.
    std::optional<std::size_t> acquireFree(const std::stop_token& stop)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait(lock, stop, [this] { return !m_free.empty(); })) {
            return std::nullopt;
        }
        const std::size_t index = m_free.front();
        m_free.pop_front();
        return index;
    }

    void pushFull(std::size_t index)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_full.push_back(index);
        }
        m_changed.notify_all();
    }

    // Scanner: next filled buffer in file order.
    std::size_t acquireFull()
    {
        std::unique_lock lock(m_mutex);
        m_changed.wait(lock, [this] { return !m_full.empty(); });
        const std::size_t index = m_full.front();
        m_full.pop_front();
        return index;
    }

    void release(std::size_t index)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_free.push_back(index);
        }
        m_changed.notify_all();
    }

    Block& operator[](std::size_t index) { return m_blocks[index]; }

private:
    std::vector<Block> m_blocks;
    std::mutex m_mutex;
    std::condition_variable_any m_changed;
    std::deque<std::size_t> m_free;
    std::deque<std::size_t> m_full;
};

}  // namespace

Result<IndexStats> Indexer::run(const File& file, LineIndex& index, const std::stop_token& stop,
                                const IndexProgressCallback& progress,
                                const IndexerOptions& options)
{
    LV_ASSERT(options.blockSize > 0 && options.bufferCount >= 2);
    const auto size = file.size();
    if (!size) {
        return tl::unexpected(size.error());
    }
    const std::uint64_t start = index.appendedEnd();
    const std::uint64_t total = *size;
    if (total < index.snapshot().indexedEnd.get() || total < start) {
        return makeError(ErrorCode::kOutOfRange,
                         "file '" + file.path().string() + "' is smaller than the indexed data");
    }

    LineSplitter splitter(index.encoding(), start);
    const LineCount linesBefore = index.snapshot().completeLines;
    IndexStats stats;
    if (start == total) {
        index.publish(total);
        return stats;
    }

    BlockRing ring(options.bufferCount, options.blockSize);
    // The reader stops on its own at the end, or when the jthread destructor requests it.
    std::jthread reader([&file, &ring, start, total](const std::stop_token& readerStop) {
        std::uint64_t offset = start;
        while (offset < total) {
            const auto slot = ring.acquireFree(readerStop);
            if (!slot) {
                return;
            }
            Block& block = ring[*slot];
            const std::uint64_t wanted = std::min<std::uint64_t>(block.data.size(), total - offset);
            block.offset = offset;
            block.error.reset();
            const auto read = file.readAt(
                ByteOffset(offset), std::span(block.data.data(), static_cast<std::size_t>(wanted)));
            if (read) {
                block.size = *read;
            } else {
                block.size = 0;
                block.error = read.error();
            }
            const bool stopReading = !read || *read < wanted;  // error, or the file shrank
            offset += block.size;
            ring.pushFull(*slot);
            if (stopReading) {
                return;
            }
        }
    });

    std::vector<std::uint64_t> lineEnds;
    lineEnds.reserve(options.blockSize / 64);
    auto lastReport = std::chrono::steady_clock::now();
    auto report = [&](bool force) {
        if (!progress) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - lastReport < options.progressInterval) {
            return;
        }
        lastReport = now;
        progress(IndexProgress{.indexedBytes = splitter.position(),
                               .totalBytes = total,
                               .lines = index.snapshot().lineCount()});
    };

    std::optional<Error> failure;
    while (splitter.position() < total) {
        if (stop.stop_requested()) {
            failure = Error{
                .code = ErrorCode::kCanceled, .systemCode = 0, .message = "indexing canceled"};
            break;
        }
        const std::size_t slot = ring.acquireFull();
        Block& block = ring[slot];
        LV_ASSERT(block.offset == splitter.position());
        if (block.error) {
            failure = std::move(block.error);
            ring.release(slot);
            break;
        }
        // Fewer bytes than requested means the file shrank while reading; the reader stops then.
        const std::uint64_t expected =
            std::min<std::uint64_t>(block.data.size(), total - block.offset);
        const bool shortRead = block.size < expected;
        lineEnds.clear();
        splitter.feed(std::span(block.data.data(), block.size), lineEnds);
        index.append(lineEnds);
        index.publish(splitter.position());
        stats.bytesRead += block.size;
        ring.release(slot);
        report(false);
        if (shortRead) {
            break;  // the file shrank while reading; the next run detects the truncation
        }
    }
    reader.request_stop();
    reader.join();

    stats.linesAdded = index.snapshot().completeLines - linesBefore;
    report(true);
    if (failure) {
        return tl::unexpected(std::move(*failure));
    }
    return stats;
}

}  // namespace lv::core
