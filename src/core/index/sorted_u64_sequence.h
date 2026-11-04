#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace lv::core
{

// An append-only, non-decreasing sequence of 64-bit values, compressed in blocks of 128
// (ADR 0007, ARCHITECTURE §3.3). Used for line end offsets (line index) and for match lists.
//
// Storage: every full block stores its first value (base), a bit width w, and the 127 deltas
// to the previous value bit-packed with w bits (w = 0..64). Typical log line lengths below 256
// bytes cost about 1.2 bytes per value; very short lines less.
//
// Concurrency: exactly one writer thread calls append() and publish(); any number of reader
// threads call the const functions at the same time. Readers only see values up to size(),
// which advances when the writer calls publish(). Full (sealed) blocks never change and are read
// without locks; only the last, partially filled block is copied under a short mutex.
//
// Thread-safety: append()/publish()/clear() single writer; everything else from any thread.
// clear() must not run concurrently with readers.
class SortedU64Sequence
{
public:
    static constexpr std::size_t kBlockSize = 128;

    SortedU64Sequence();
    SortedU64Sequence(const SortedU64Sequence&) = delete;
    SortedU64Sequence& operator=(const SortedU64Sequence&) = delete;
    SortedU64Sequence(SortedU64Sequence&&) = delete;
    SortedU64Sequence& operator=(SortedU64Sequence&&) = delete;
    ~SortedU64Sequence();

    // Writer: appends a value that must be >= the previous one. Not visible before publish().
    void append(std::uint64_t value);

    // Writer: appends several values (non-decreasing, >= the previous one). Same as calling
    // append() for each, but much faster.
    void append(std::span<const std::uint64_t> values);

    // Writer: makes all appended values visible to readers.
    void publish();

    // Writer: removes all values. No reader may access the sequence concurrently.
    void clear();

    // Writer: number of appended values, published or not.
    [[nodiscard]] std::size_t appendedCount() const noexcept { return m_appended; }

    // Number of published values. O(1).
    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_published.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    // Value at `index` (< size()). O(kBlockSize) worst case.
    [[nodiscard]] std::uint64_t at(std::size_t index) const;

    // Index of the first published value >= `value`, or size() if there is none. O(log n + block).
    [[nodiscard]] std::size_t lowerBound(std::uint64_t value) const;

    // Copies values [first, first + out.size()) into `out`; requires first + out.size() <= size().
    // Decodes each block once, so this is the fast way to read many consecutive values.
    void copy(std::size_t first, std::span<std::uint64_t> out) const;

    // Bytes in use by the published and pending data (headers, packed words, the open block).
    [[nodiscard]] std::size_t memoryBytes() const noexcept;

private:
    struct BlockHeader
    {
        std::uint64_t base = 0;
        const std::uint64_t* words = nullptr;
        std::uint8_t width = 0;
    };

    // Headers live in segments of geometrically growing size that never move.
    static constexpr std::size_t kFirstSegmentBlocks = 1024;
    static constexpr std::size_t kMaxSegments = 40;
    static constexpr std::size_t kPageWords = 8192;  // 64 KiB pages for packed deltas

    static std::size_t segmentOf(std::size_t block, std::size_t& offsetInSegment) noexcept;
    [[nodiscard]] const BlockHeader& header(std::size_t block) const noexcept;

    [[nodiscard]] std::size_t sealedBlocks() const noexcept
    {
        return m_sealedBlocks.load(std::memory_order_acquire);
    }

    void sealBlock();
    std::uint64_t* allocateWords(std::size_t count);

    static std::uint64_t decodeOne(const BlockHeader& block, std::size_t indexInBlock) noexcept;
    static void decodeAll(const BlockHeader& block,
                          std::array<std::uint64_t, kBlockSize>& out) noexcept;

    // Fills `out` with the published values of `block` and returns how many are valid.
    std::size_t readBlock(std::size_t block, std::array<std::uint64_t, kBlockSize>& out) const;

    // Writer-only state.
    std::array<std::uint64_t, kBlockSize> m_pending{};
    std::size_t m_appended = 0;
    std::uint64_t m_last = 0;
    std::size_t m_bytesUsed = 0;
    std::vector<std::unique_ptr<std::array<std::uint64_t, kPageWords>>> m_pages;
    std::size_t m_pageUsed = kPageWords;

    // Shared state.
    // Each segment is sized once when it is created and never resized, so its data never moves;
    // readers only touch segments that hold sealed blocks.
    std::array<std::vector<BlockHeader>, kMaxSegments> m_segments;
    std::atomic<std::size_t> m_sealedBlocks{0};
    std::atomic<std::size_t> m_published{0};

    // The published part of the open (last, not yet full) block.
    mutable std::mutex m_openMutex;
    std::array<std::uint64_t, kBlockSize> m_open{};
    std::size_t m_openBlock = 0;
    std::size_t m_openCount = 0;
};

}  // namespace lv::core
