#pragma once

#include "core/index/line_index.h"
#include "core/platform/file.h"
#include "core/util/result.h"
#include "core/util/strong_types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace lv::core
{

// The raw bytes of several consecutive lines, without line terminators (a CR before the LF is
// removed). Lines longer than the reader's limit are cut and flagged.
struct LineBlock
{
    struct Line
    {
        std::size_t offset = 0;  // into bytes
        std::size_t length = 0;
        bool truncated = false;  // longer than LineReaderOptions::maxLineBytes
    };

    LineNumber first;
    std::vector<std::byte> bytes;
    std::vector<Line> lines;

    [[nodiscard]] std::span<const std::byte> line(std::size_t index) const
    {
        const Line& entry = lines.at(index);
        return std::span<const std::byte>(bytes).subspan(entry.offset, entry.length);
    }
};

struct LineReaderOptions
{
    // Lines are read up to this many bytes (SPEC F-FILE-7).
    std::size_t maxLineBytes = std::size_t{16} * 1024 * 1024;
    // Page cache: page size and number of cached pages (default 64 KiB × 256 = 16 MiB).
    std::size_t pageSize = std::size_t{64} * 1024;
    std::size_t cachePages = 256;
};

// Reads lines of a file through a small LRU page cache (ARCHITECTURE §3.5). Consecutive lines
// are read with as few positional reads as possible.
//
// The reader never returns garbage for a file that changed under it: if the file is shorter
// than the indexed range, readLines() fails with kOutOfRange. After the document resets its
// index (reload, truncation, rotation) it must call clearCache().
//
// Thread-safety: all functions may be called concurrently.
class LineReader
{
public:
    LineReader(const File& file, const LineIndex& index, LineReaderOptions options = {});

    // Reads lines [first, first + count) of `snapshot`. Requires first + count <= line count.
    [[nodiscard]] Result<LineBlock> readLines(const LineIndex::Snapshot& snapshot, LineNumber first,
                                              LineCount count) const;

    void clearCache();

    // Number of File::readAt calls so far (test and benchmark hook).
    [[nodiscard]] std::uint64_t readCalls() const noexcept
    {
        return m_readCalls.load(std::memory_order_relaxed);
    }

    [[nodiscard]] const LineReaderOptions& options() const noexcept { return m_options; }

private:
    struct Page
    {
        std::vector<std::byte> data;  // may be shorter than pageSize at the end of the file
        std::list<std::uint64_t>::iterator lru;
    };

    // Copies [offset, offset + out.size()) into out, reading missing pages in contiguous runs.
    [[nodiscard]] Result<void> readRange(std::uint64_t offset, std::span<std::byte> out) const;
    [[nodiscard]] Result<void> readDirect(std::uint64_t offset, std::span<std::byte> out) const;

    const File* m_file;
    const LineIndex* m_index;
    LineReaderOptions m_options;

    mutable std::mutex m_cacheMutex;
    mutable std::unordered_map<std::uint64_t, Page> m_pages;
    mutable std::list<std::uint64_t> m_lru;  // most recently used first
    mutable std::atomic<std::uint64_t> m_readCalls{0};
};

}  // namespace lv::core
