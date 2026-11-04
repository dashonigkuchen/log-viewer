#include "core/index/sorted_u64_sequence.h"

#include "core/util/assert.h"

#include <algorithm>
#include <bit>
#include <span>

namespace lv::core
{

namespace
{

constexpr std::size_t kDeltasPerBlock = SortedU64Sequence::kBlockSize - 1;

constexpr std::size_t wordsForWidth(std::size_t width) noexcept
{
    return ((kDeltasPerBlock * width) + 63) / 64;
}

constexpr std::uint64_t maskForWidth(std::size_t width) noexcept
{
    return width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

std::uint64_t readDelta(const std::uint64_t* words, std::size_t width, std::size_t index) noexcept
{
    const std::size_t bit = index * width;
    const std::size_t word = bit / 64;
    const std::size_t offset = bit % 64;
    std::uint64_t value = words[word] >> offset;
    if (offset + width > 64) {
        value |= words[word + 1] << (64 - offset);
    }
    return value & maskForWidth(width);
}

}  // namespace

SortedU64Sequence::SortedU64Sequence() = default;
SortedU64Sequence::~SortedU64Sequence() = default;

std::size_t SortedU64Sequence::segmentOf(std::size_t block, std::size_t& offsetInSegment) noexcept
{
    // Segment s holds kFirstSegmentBlocks << s blocks and starts at kFirstSegmentBlocks * (2^s -
    // 1).
    const std::size_t scaled = (block / kFirstSegmentBlocks) + 1;
    const auto segment = static_cast<std::size_t>(std::bit_width(scaled) - 1);
    offsetInSegment = block - (kFirstSegmentBlocks * ((std::size_t{1} << segment) - 1));
    return segment;
}

const SortedU64Sequence::BlockHeader& SortedU64Sequence::header(std::size_t block) const noexcept
{
    std::size_t offset = 0;
    const std::size_t segment = segmentOf(block, offset);
    return m_segments.at(segment)[offset];
}

std::uint64_t* SortedU64Sequence::allocateWords(std::size_t count)
{
    if (m_pageUsed + count > kPageWords) {
        m_pages.push_back(std::make_unique<std::array<std::uint64_t, kPageWords>>());
        m_pageUsed = 0;
    }
    std::uint64_t* words = m_pages.back()->data() + m_pageUsed;
    m_pageUsed += count;
    std::fill_n(words, count, std::uint64_t{0});
    return words;
}

void SortedU64Sequence::append(std::uint64_t value)
{
    LV_ASSERT(m_appended == 0 || value >= m_last);
    m_last = value;
    const std::size_t indexInBlock = m_appended % kBlockSize;
    m_pending.at(indexInBlock) = value;
    ++m_appended;
    if (indexInBlock == kBlockSize - 1) {
        sealBlock();
    }
}

void SortedU64Sequence::sealBlock()
{
    std::uint64_t maxDelta = 0;
    for (std::size_t i = 1; i < kBlockSize; ++i) {
        maxDelta = std::max(maxDelta, m_pending.at(i) - m_pending.at(i - 1));
    }
    const auto width = static_cast<std::size_t>(std::bit_width(maxDelta));
    const std::size_t wordCount = wordsForWidth(width);
    std::uint64_t* words = wordCount == 0 ? nullptr : allocateWords(wordCount);
    for (std::size_t i = 1; i < kBlockSize && width != 0; ++i) {
        const std::uint64_t delta = m_pending.at(i) - m_pending.at(i - 1);
        const std::size_t bit = (i - 1) * width;
        const std::size_t word = bit / 64;
        const std::size_t offset = bit % 64;
        words[word] |= delta << offset;
        if (offset + width > 64) {
            words[word + 1] |= delta >> (64 - offset);
        }
    }

    const std::size_t block = m_sealedBlocks.load(std::memory_order_relaxed);
    std::size_t offset = 0;
    const std::size_t segment = segmentOf(block, offset);
    LV_ASSERT(segment < kMaxSegments);
    if (offset == 0) {
        m_segments.at(segment).resize(kFirstSegmentBlocks << segment);
    }
    m_segments.at(segment)[offset] = BlockHeader{
        .base = m_pending.at(0), .words = words, .width = static_cast<std::uint8_t>(width)};
    m_bytesUsed += sizeof(BlockHeader) + (wordCount * sizeof(std::uint64_t));
    m_sealedBlocks.store(block + 1, std::memory_order_release);
}

void SortedU64Sequence::publish()
{
    const std::size_t count = m_appended % kBlockSize;
    {
        const std::scoped_lock lock(m_openMutex);
        m_openBlock = m_appended / kBlockSize;
        m_openCount = count;
        std::copy_n(m_pending.begin(), count, m_open.begin());
    }
    m_published.store(m_appended, std::memory_order_release);
}

void SortedU64Sequence::clear()
{
    const std::scoped_lock lock(m_openMutex);
    m_published.store(0, std::memory_order_release);
    m_sealedBlocks.store(0, std::memory_order_release);
    m_appended = 0;
    m_last = 0;
    m_bytesUsed = 0;
    m_openBlock = 0;
    m_openCount = 0;
    m_pages.clear();
    m_pageUsed = kPageWords;
    for (auto& segment : m_segments) {
        segment = {};
    }
}

std::uint64_t SortedU64Sequence::decodeOne(const BlockHeader& block,
                                           std::size_t indexInBlock) noexcept
{
    std::uint64_t value = block.base;
    if (block.width == 0) {
        return value;  // all values in the block are equal; no packed words exist
    }
    for (std::size_t i = 0; i < indexInBlock; ++i) {
        value += readDelta(block.words, block.width, i);
    }
    return value;
}

void SortedU64Sequence::decodeAll(const BlockHeader& block,
                                  std::array<std::uint64_t, kBlockSize>& out) noexcept
{
    out[0] = block.base;
    if (block.width == 0) {
        std::fill(out.begin() + 1, out.end(), block.base);
        return;
    }
    for (std::size_t i = 1; i < kBlockSize; ++i) {
        out.at(i) = out.at(i - 1) + readDelta(block.words, block.width, i - 1);
    }
}

std::size_t SortedU64Sequence::readBlock(std::size_t block,
                                         std::array<std::uint64_t, kBlockSize>& out) const
{
    if (block < sealedBlocks()) {
        decodeAll(header(block), out);
        return kBlockSize;
    }
    const std::scoped_lock lock(m_openMutex);
    if (block < sealedBlocks()) {
        decodeAll(header(block), out);
        return kBlockSize;
    }
    LV_ASSERT(block == m_openBlock);
    std::copy_n(m_open.begin(), m_openCount, out.begin());
    return m_openCount;
}

std::uint64_t SortedU64Sequence::at(std::size_t index) const
{
    LV_ASSERT(index < size());
    const std::size_t block = index / kBlockSize;
    const std::size_t indexInBlock = index % kBlockSize;
    if (block < sealedBlocks()) {
        return decodeOne(header(block), indexInBlock);
    }
    const std::scoped_lock lock(m_openMutex);
    if (block < sealedBlocks()) {
        return decodeOne(header(block), indexInBlock);
    }
    LV_ASSERT(block == m_openBlock && indexInBlock < m_openCount);
    return m_open.at(indexInBlock);
}

void SortedU64Sequence::copy(std::size_t first, std::span<std::uint64_t> out) const
{
    LV_ASSERT(first + out.size() <= size());
    std::array<std::uint64_t, kBlockSize> values{};
    std::size_t done = 0;
    while (done < out.size()) {
        const std::size_t index = first + done;
        const std::size_t block = index / kBlockSize;
        const std::size_t begin = index % kBlockSize;
        const std::size_t valid = readBlock(block, values);
        const std::size_t count = std::min(valid - begin, out.size() - done);
        std::copy_n(values.begin() + static_cast<std::ptrdiff_t>(begin), count,
                    out.begin() + static_cast<std::ptrdiff_t>(done));
        done += count;
    }
}

std::size_t SortedU64Sequence::lowerBound(std::uint64_t value) const
{
    const std::size_t count = size();
    const std::size_t fullBlocks = std::min(sealedBlocks(), count / kBlockSize);

    // First full block whose base is >= value.
    std::size_t low = 0;
    std::size_t high = fullBlocks;
    while (low < high) {
        const std::size_t middle = low + ((high - low) / 2);
        if (header(middle).base < value) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    std::array<std::uint64_t, kBlockSize> values{};
    if (low > 0) {
        // The answer may be inside the previous block.
        decodeAll(header(low - 1), values);
        const auto position =
            static_cast<std::size_t>(std::ranges::lower_bound(values, value) - values.begin());
        if (position < kBlockSize) {
            return ((low - 1) * kBlockSize) + position;
        }
    }
    if (low < fullBlocks) {
        return low * kBlockSize;
    }
    // Remaining published values after the full blocks (at most one block).
    const std::size_t tailStart = fullBlocks * kBlockSize;
    if (tailStart >= count) {
        return count;
    }
    const std::size_t valid = std::min(readBlock(fullBlocks, values), count - tailStart);
    const std::span<const std::uint64_t> tail(values.data(), valid);
    return tailStart +
           static_cast<std::size_t>(std::ranges::lower_bound(tail, value) - tail.begin());
}

std::size_t SortedU64Sequence::memoryBytes() const noexcept
{
    return m_bytesUsed + sizeof(*this);
}

}  // namespace lv::core
