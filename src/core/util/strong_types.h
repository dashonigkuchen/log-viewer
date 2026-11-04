#pragma once

#include "core/util/assert.h"

#include <compare>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>

// Strong 64-bit types for line numbers, line counts, and byte offsets (AGENTS.md §5).
// They prevent mixing up lines and bytes and catch overflow with LV_ASSERT.
// Line numbers are 0-based; the UI adds 1 when displaying them.
namespace lv::core
{

class LineCount;

// Number of lines.
class LineCount
{
public:
    constexpr LineCount() noexcept = default;

    constexpr explicit LineCount(std::uint64_t value) noexcept
        : m_value(value)
    {}

    [[nodiscard]] constexpr std::uint64_t get() const noexcept { return m_value; }

    constexpr auto operator<=>(const LineCount&) const noexcept = default;

    constexpr LineCount& operator+=(LineCount other)
    {
        LV_ASSERT(other.m_value <= std::numeric_limits<std::uint64_t>::max() - m_value);
        m_value += other.m_value;
        return *this;
    }

    constexpr LineCount& operator-=(LineCount other)
    {
        LV_ASSERT(other.m_value <= m_value);
        m_value -= other.m_value;
        return *this;
    }

    [[nodiscard]] friend constexpr LineCount operator+(LineCount a, LineCount b) { return a += b; }

    [[nodiscard]] friend constexpr LineCount operator-(LineCount a, LineCount b) { return a -= b; }

private:
    std::uint64_t m_value = 0;
};

// 0-based index of a line in a document.
class LineNumber
{
public:
    constexpr LineNumber() noexcept = default;

    constexpr explicit LineNumber(std::uint64_t value) noexcept
        : m_value(value)
    {}

    [[nodiscard]] constexpr std::uint64_t get() const noexcept { return m_value; }

    // 1-based number for display.
    [[nodiscard]] constexpr std::uint64_t displayNumber() const
    {
        LV_ASSERT(m_value < std::numeric_limits<std::uint64_t>::max());
        return m_value + 1;
    }

    constexpr auto operator<=>(const LineNumber&) const noexcept = default;

    constexpr LineNumber& operator+=(LineCount count)
    {
        LV_ASSERT(count.get() <= std::numeric_limits<std::uint64_t>::max() - m_value);
        m_value += count.get();
        return *this;
    }

    constexpr LineNumber& operator-=(LineCount count)
    {
        LV_ASSERT(count.get() <= m_value);
        m_value -= count.get();
        return *this;
    }

    constexpr LineNumber& operator++() { return *this += LineCount(1); }

    constexpr LineNumber& operator--() { return *this -= LineCount(1); }

    [[nodiscard]] friend constexpr LineNumber operator+(LineNumber n, LineCount c)
    {
        return n += c;
    }

    [[nodiscard]] friend constexpr LineNumber operator-(LineNumber n, LineCount c)
    {
        return n -= c;
    }

    // Distance between two line numbers; requires a >= b.
    [[nodiscard]] friend constexpr LineCount operator-(LineNumber a, LineNumber b)
    {
        LV_ASSERT(a.m_value >= b.m_value);
        return LineCount(a.m_value - b.m_value);
    }

private:
    std::uint64_t m_value = 0;
};

// Offset in bytes into a file.
class ByteOffset
{
public:
    constexpr ByteOffset() noexcept = default;

    constexpr explicit ByteOffset(std::uint64_t value) noexcept
        : m_value(value)
    {}

    [[nodiscard]] constexpr std::uint64_t get() const noexcept { return m_value; }

    constexpr auto operator<=>(const ByteOffset&) const noexcept = default;

    constexpr ByteOffset& operator+=(std::uint64_t bytes)
    {
        LV_ASSERT(bytes <= std::numeric_limits<std::uint64_t>::max() - m_value);
        m_value += bytes;
        return *this;
    }

    constexpr ByteOffset& operator-=(std::uint64_t bytes)
    {
        LV_ASSERT(bytes <= m_value);
        m_value -= bytes;
        return *this;
    }

    [[nodiscard]] friend constexpr ByteOffset operator+(ByteOffset o, std::uint64_t bytes)
    {
        return o += bytes;
    }

    [[nodiscard]] friend constexpr ByteOffset operator-(ByteOffset o, std::uint64_t bytes)
    {
        return o -= bytes;
    }

    // Number of bytes between two offsets; requires a >= b.
    [[nodiscard]] friend constexpr std::uint64_t operator-(ByteOffset a, ByteOffset b)
    {
        LV_ASSERT(a.m_value >= b.m_value);
        return a.m_value - b.m_value;
    }

private:
    std::uint64_t m_value = 0;
};

// Half-open byte range [begin, end). Invariant: begin <= end.
class ByteRange
{
public:
    constexpr ByteRange() noexcept = default;

    constexpr ByteRange(ByteOffset begin, ByteOffset end)
        : m_begin(begin)
        , m_end(end)
    {
        LV_ASSERT(begin <= end);
    }

    [[nodiscard]] static constexpr ByteRange fromSize(ByteOffset begin, std::uint64_t size)
    {
        return {begin, begin + size};
    }

    [[nodiscard]] constexpr ByteOffset begin() const noexcept { return m_begin; }

    [[nodiscard]] constexpr ByteOffset end() const noexcept { return m_end; }

    [[nodiscard]] constexpr std::uint64_t size() const noexcept
    {
        return m_end.get() - m_begin.get();
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return m_begin == m_end; }

    [[nodiscard]] constexpr bool contains(ByteOffset offset) const noexcept
    {
        return offset >= m_begin && offset < m_end;
    }

    constexpr bool operator==(const ByteRange&) const noexcept = default;

private:
    ByteOffset m_begin;
    ByteOffset m_end;
};

[[nodiscard]] std::string toString(LineNumber value);
[[nodiscard]] std::string toString(LineCount value);
[[nodiscard]] std::string toString(ByteOffset value);
[[nodiscard]] std::string toString(ByteRange value);

std::ostream& operator<<(std::ostream& out, LineNumber value);
std::ostream& operator<<(std::ostream& out, LineCount value);
std::ostream& operator<<(std::ostream& out, ByteOffset value);
std::ostream& operator<<(std::ostream& out, ByteRange value);

}  // namespace lv::core
