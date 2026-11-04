#include "core/index/line_splitter.h"

#include <cstring>

namespace lv::core
{

namespace
{

constexpr auto kLineFeed = std::byte{0x0A};
constexpr auto kZero = std::byte{0x00};

}  // namespace

LineSplitter::LineSplitter(Encoding encoding, std::uint64_t startOffset) noexcept
    : m_utf16(encoding == Encoding::kUtf16Le || encoding == Encoding::kUtf16Be)
    , m_bigEndian(encoding == Encoding::kUtf16Be)
    , m_position(startOffset)
{}

void LineSplitter::feed(std::span<const std::byte> data, std::vector<std::uint64_t>& lineEnds)
{
    if (m_utf16) {
        feedUtf16(data, lineEnds);
    } else {
        feedSingleByte(data, lineEnds);
    }
}

void LineSplitter::feedSingleByte(std::span<const std::byte> data,
                                  std::vector<std::uint64_t>& lineEnds)
{
    const std::byte* const begin = data.data();
    const std::byte* const end = begin + data.size();
    const std::byte* cursor = begin;
    while (cursor < end) {
        const void* found = std::memchr(cursor, 0x0A, static_cast<std::size_t>(end - cursor));
        if (found == nullptr) {
            break;
        }
        const auto* lineFeed = static_cast<const std::byte*>(found);
        lineEnds.push_back(m_position + static_cast<std::uint64_t>(lineFeed - begin) + 1);
        cursor = lineFeed + 1;
    }
    m_position += data.size();
}

void LineSplitter::feedUtf16(std::span<const std::byte> data, std::vector<std::uint64_t>& lineEnds)
{
    if (data.empty()) {
        return;
    }
    // The code-unit layout: little endian "0A 00", big endian "00 0A".
    const std::byte first = m_bigEndian ? kZero : kLineFeed;
    const std::byte second = m_bigEndian ? kLineFeed : kZero;

    std::size_t index = 0;
    if (m_carry) {
        if (*m_carry == first && data[0] == second) {
            lineEnds.push_back(m_position + 1);
        }
        m_carry.reset();
        index = 1;
    }
    // `index` is now at a code-unit boundary relative to data.
    const std::byte* const begin = data.data();
    const std::size_t size = data.size();
    while (index + 1 < size) {
        // Search for the line feed byte (0x0A) and check that it sits at the right position of
        // a code unit. This skips most of the data with memchr.
        const void* found = std::memchr(begin + index, 0x0A, size - index);
        if (found == nullptr) {
            break;
        }
        const auto position =
            static_cast<std::size_t>(static_cast<const std::byte*>(found) - begin);
        const std::size_t unitStart = position - ((position - index) % 2);
        if (unitStart + 1 >= size) {
            index = unitStart;
            break;
        }
        if (begin[unitStart] == first && begin[unitStart + 1] == second) {
            lineEnds.push_back(m_position + unitStart + 2);
        }
        index = unitStart + 2;
    }
    if (index < size && (size - index) % 2 == 1) {
        m_carry = begin[size - 1];
    }
    m_position += size;
}

}  // namespace lv::core
