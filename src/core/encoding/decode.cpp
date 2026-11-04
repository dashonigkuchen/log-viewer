#include "core/encoding/decode.h"

#include "core/util/assert.h"

#include <algorithm>
#include <array>

namespace lv::core
{

namespace
{

// Windows-1252 bytes 0x80..0x9F (WHATWG mapping; undefined bytes map to the C1 code point).
constexpr std::array<char16_t, 32> kWindows1252High{
    u'\u20AC', u'\u0081', u'\u201A', u'\u0192', u'\u201E', u'\u2026', u'\u2020', u'\u2021',
    u'\u02C6', u'\u2030', u'\u0160', u'\u2039', u'\u0152', u'\u008D', u'\u017D', u'\u008F',
    u'\u0090', u'\u2018', u'\u2019', u'\u201C', u'\u201D', u'\u2022', u'\u2013', u'\u2014',
    u'\u02DC', u'\u2122', u'\u0161', u'\u203A', u'\u0153', u'\u009D', u'\u017E', u'\u0178',
};

class Output
{
public:
    Output(std::u16string& text, std::vector<std::uint32_t>* offsets, DecodeOptions options)
        : m_text(&text)
        , m_offsets(offsets)
        , m_options(options)
    {
        m_text->clear();
        if (m_offsets != nullptr) {
            m_offsets->clear();
        }
    }

    void codePoint(char32_t value, std::size_t sourceOffset)
    {
        if (m_options.visibleControls) {
            if (value < 0x20 && value != U'\t') {
                value += 0x2400;
            } else if (value == 0x7F) {
                value = 0x2421;
            }
        }
        if (value >= 0x10000) {
            value -= 0x10000;
            unit(static_cast<char16_t>(0xD800 + (value >> 10U)), sourceOffset);
            unit(static_cast<char16_t>(0xDC00 + (value & 0x3FFU)), sourceOffset);
        } else {
            unit(static_cast<char16_t>(value), sourceOffset);
        }
    }

    void finish(std::size_t rawSize)
    {
        if (m_offsets != nullptr) {
            m_offsets->push_back(static_cast<std::uint32_t>(rawSize));
        }
    }

private:
    void unit(char16_t value, std::size_t sourceOffset)
    {
        m_text->push_back(value);
        if (m_offsets != nullptr) {
            m_offsets->push_back(static_cast<std::uint32_t>(sourceOffset));
        }
    }

    std::u16string* m_text;
    std::vector<std::uint32_t>* m_offsets;
    DecodeOptions m_options;
};

unsigned byteAt(std::span<const std::byte> raw, std::size_t index)
{
    return std::to_integer<unsigned>(raw[index]);
}

struct Utf8Sequence
{
    char32_t value = 0;
    std::size_t consumed = 1;
    bool valid = false;
};

// Decodes the sequence starting with a non-ASCII lead byte at `i`. An invalid sequence consumes
// its maximal valid prefix (one U+FFFD per prefix, as recommended by Unicode).
Utf8Sequence decodeUtf8Sequence(std::span<const std::byte> raw, std::size_t i)
{
    const unsigned lead = byteAt(raw, i);
    // Expected length and the valid range of the second byte (Unicode Table 3-7).
    std::size_t length = 0;
    unsigned low = 0x80U;
    unsigned high = 0xBFU;
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3;
        low = lead == 0xE0U ? 0xA0U : 0x80U;
        high = lead == 0xEDU ? 0x9FU : 0xBFU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4;
        low = lead == 0xF0U ? 0x90U : 0x80U;
        high = lead == 0xF4U ? 0x8FU : 0xBFU;
    }
    Utf8Sequence sequence;
    if (length == 0) {
        return sequence;
    }
    sequence.value = lead & (0xFFU >> (length + 1));
    for (; sequence.consumed < length; ++sequence.consumed) {
        if (i + sequence.consumed >= raw.size()) {
            return sequence;
        }
        const unsigned next = byteAt(raw, i + sequence.consumed);
        const unsigned min = sequence.consumed == 1 ? low : 0x80U;
        const unsigned max = sequence.consumed == 1 ? high : 0xBFU;
        if (next < min || next > max) {
            return sequence;
        }
        sequence.value = (sequence.value << 6U) | (next & 0x3FU);
    }
    sequence.valid = true;
    return sequence;
}

void decodeUtf8(std::span<const std::byte> raw, Output& out)
{
    std::size_t i = 0;
    while (i < raw.size()) {
        const unsigned lead = byteAt(raw, i);
        if (lead < 0x80U) {
            out.codePoint(lead, i);
            ++i;
            continue;
        }
        const Utf8Sequence sequence = decodeUtf8Sequence(raw, i);
        out.codePoint(
            sequence.valid ? sequence.value : static_cast<char32_t>(kReplacementCharacter), i);
        i += sequence.consumed;
    }
}

void decodeUtf16(std::span<const std::byte> raw, bool bigEndian, Output& out)
{
    auto unitAt = [&](std::size_t index) -> char16_t {
        const unsigned a = byteAt(raw, index);
        const unsigned b = byteAt(raw, index + 1);
        return static_cast<char16_t>(bigEndian ? ((a << 8U) | b) : ((b << 8U) | a));
    };
    std::size_t i = 0;
    while (i + 1 < raw.size()) {
        const char16_t unit = unitAt(i);
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < raw.size()) {
            const char16_t next = unitAt(i + 2);
            if (next >= 0xDC00 && next <= 0xDFFF) {
                const char32_t value =
                    0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10U) + (next - 0xDC00);
                out.codePoint(value, i);
                i += 4;
                continue;
            }
        }
        const bool surrogate = unit >= 0xD800 && unit <= 0xDFFF;
        out.codePoint(surrogate ? kReplacementCharacter : unit, i);
        i += 2;
    }
    if (i < raw.size()) {
        out.codePoint(kReplacementCharacter, i);  // odd trailing byte
    }
}

void decodeSingleByte(std::span<const std::byte> raw, bool windows1252, Output& out)
{
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const unsigned value = byteAt(raw, i);
        if (windows1252 && value >= 0x80U && value <= 0x9FU) {
            out.codePoint(kWindows1252High.at(value - 0x80U), i);
        } else {
            out.codePoint(value, i);
        }
    }
}

}  // namespace

void decodeLine(std::span<const std::byte> raw, Encoding encoding, std::u16string& out,
                std::vector<std::uint32_t>* sourceOffsets, DecodeOptions options)
{
    out.reserve(raw.size());
    Output output(out, sourceOffsets, options);
    switch (encoding) {
    case Encoding::kUtf8:
        decodeUtf8(raw, output);
        break;
    case Encoding::kUtf16Le:
        decodeUtf16(raw, false, output);
        break;
    case Encoding::kUtf16Be:
        decodeUtf16(raw, true, output);
        break;
    case Encoding::kWindows1252:
        decodeSingleByte(raw, true, output);
        break;
    case Encoding::kIso88591:
        decodeSingleByte(raw, false, output);
        break;
    }
    output.finish(raw.size());
}

std::u16string decodeLine(std::span<const std::byte> raw, Encoding encoding, DecodeOptions options)
{
    std::u16string text;
    decodeLine(raw, encoding, text, nullptr, options);
    return text;
}

std::size_t utf16OffsetForByte(std::span<const std::uint32_t> sourceOffsets,
                               std::uint32_t byteOffset) noexcept
{
    LV_ASSERT(!sourceOffsets.empty());
    // Last unit whose character starts at or before the byte offset.
    const auto* upper = std::upper_bound(sourceOffsets.data(),
                                         sourceOffsets.data() + sourceOffsets.size(), byteOffset);
    if (upper == sourceOffsets.data()) {
        return 0;
    }
    auto index = static_cast<std::size_t>(upper - sourceOffsets.data()) - 1;
    // Move to the first unit of that character (surrogate pairs share an offset).
    while (index > 0 && sourceOffsets[index - 1] == sourceOffsets[index]) {
        --index;
    }
    return index;
}

std::uint32_t byteOffsetForUtf16(std::span<const std::uint32_t> sourceOffsets,
                                 std::size_t utf16Offset) noexcept
{
    LV_ASSERT(utf16Offset < sourceOffsets.size());
    return sourceOffsets[utf16Offset];
}

std::vector<std::uint32_t> displayColumns(std::u16string_view text, std::uint32_t tabWidth)
{
    LV_ASSERT(tabWidth > 0);
    std::vector<std::uint32_t> columns;
    columns.reserve(text.size() + 1);
    std::uint32_t column = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t unit = text[i];
        columns.push_back(column);
        if (unit >= 0xDC00 && unit <= 0xDFFF && i > 0 && text[i - 1] >= 0xD800 &&
            text[i - 1] <= 0xDBFF) {
            columns.back() = columns[i - 1];  // low surrogate shares the column
            continue;
        }
        column = unit == u'\t' ? ((column / tabWidth) + 1) * tabWidth : column + 1;
    }
    columns.push_back(column);
    return columns;
}

std::u16string expandTabs(std::u16string_view text, std::uint32_t tabWidth)
{
    LV_ASSERT(tabWidth > 0);
    std::u16string out;
    out.reserve(text.size());
    std::uint32_t column = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t unit = text[i];
        if (unit == u'\t') {
            const std::uint32_t next = ((column / tabWidth) + 1) * tabWidth;
            out.append(next - column, u' ');
            column = next;
            continue;
        }
        out.push_back(unit);
        const bool lowSurrogate = unit >= 0xDC00 && unit <= 0xDFFF && i > 0 &&
                                  text[i - 1] >= 0xD800 && text[i - 1] <= 0xDBFF;
        if (!lowSurrogate) {
            ++column;
        }
    }
    return out;
}

}  // namespace lv::core
