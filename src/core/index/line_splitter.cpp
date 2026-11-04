#include "core/index/line_splitter.h"

#include <algorithm>
#include <bit>
#include <cstring>

// SSE2 is part of every x86-64 CPU; other architectures use memchr.
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
// A macro because it selects code with #if, which a constexpr variable cannot.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LV_SPLITTER_SSE2 1
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LV_SPLITTER_SSE2 0
#endif

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
#if LV_SPLITTER_SSE2
    // Compares 64 bytes per step against '\n' and extracts the positions from the bit mask.
    // The first eight positions of a step are written unconditionally into a scratch buffer
    // (entries past the real count are overwritten later), which avoids a branch per line. With
    // short lines this is about twice as fast as one memchr call per line (roadmap M1-10).
    constexpr std::size_t kChunk = std::size_t{64} * 1024;
    constexpr std::size_t kSlack = 64;
    if (m_scratch.size() < kChunk + kSlack) {
        m_scratch.resize(kChunk + kSlack);
    }
    const __m128i lineFeed = _mm_set1_epi8('\n');
    auto mask16 = [&lineFeed](const unsigned char* p) {
        return static_cast<std::uint64_t>(static_cast<std::uint32_t>(_mm_movemask_epi8(
            _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)), lineFeed))));
    };
    const auto* const begin = reinterpret_cast<const unsigned char*>(data.data());
    for (std::size_t chunkStart = 0; chunkStart < data.size(); chunkStart += kChunk) {
        const std::size_t chunkEnd = std::min(data.size(), chunkStart + kChunk);
        std::uint64_t* const out = m_scratch.data();
        std::size_t count = 0;
        std::size_t i = chunkStart;
        for (; i + 64 <= chunkEnd; i += 64) {
            const unsigned char* chunk = begin + i;
            std::uint64_t mask = mask16(chunk) | (mask16(chunk + 16) << 16U) |
                                 (mask16(chunk + 32) << 32U) | (mask16(chunk + 48) << 48U);
            if (mask == 0) {
                continue;
            }
            const auto bits = static_cast<std::size_t>(std::popcount(mask));
            const std::uint64_t base = m_position + i + 1;
            std::uint64_t* const target = out + count;
            for (std::size_t k = 0; k < 8; ++k) {
                target[k] = base + static_cast<std::uint64_t>(std::countr_zero(mask));
                mask &= mask - 1;
            }
            for (std::size_t k = 8; k < bits; ++k) {
                target[k] = base + static_cast<std::uint64_t>(std::countr_zero(mask));
                mask &= mask - 1;
            }
            count += bits;
        }
        for (; i < chunkEnd; ++i) {
            if (begin[i] == '\n') {
                out[count++] = m_position + i + 1;
            }
        }
        lineEnds.insert(lineEnds.end(), out, out + count);
    }
#else
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
#endif
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
