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

// Bytes 0x80..0xFF; generated from the Unicode mappings (undefined bytes map to the
// code point of the byte, as the WHATWG Windows-1252 mapping does).
constexpr std::array<char16_t, 128> kWindows1250High{
    u'\u20AC', u'\u0081', u'\u201A', u'\u0083', u'\u201E', u'\u2026', u'\u2020', u'\u2021',
    u'\u0088', u'\u2030', u'\u0160', u'\u2039', u'\u015A', u'\u0164', u'\u017D', u'\u0179',
    u'\u0090', u'\u2018', u'\u2019', u'\u201C', u'\u201D', u'\u2022', u'\u2013', u'\u2014',
    u'\u0098', u'\u2122', u'\u0161', u'\u203A', u'\u015B', u'\u0165', u'\u017E', u'\u017A',
    u'\u00A0', u'\u02C7', u'\u02D8', u'\u0141', u'\u00A4', u'\u0104', u'\u00A6', u'\u00A7',
    u'\u00A8', u'\u00A9', u'\u015E', u'\u00AB', u'\u00AC', u'\u00AD', u'\u00AE', u'\u017B',
    u'\u00B0', u'\u00B1', u'\u02DB', u'\u0142', u'\u00B4', u'\u00B5', u'\u00B6', u'\u00B7',
    u'\u00B8', u'\u0105', u'\u015F', u'\u00BB', u'\u013D', u'\u02DD', u'\u013E', u'\u017C',
    u'\u0154', u'\u00C1', u'\u00C2', u'\u0102', u'\u00C4', u'\u0139', u'\u0106', u'\u00C7',
    u'\u010C', u'\u00C9', u'\u0118', u'\u00CB', u'\u011A', u'\u00CD', u'\u00CE', u'\u010E',
    u'\u0110', u'\u0143', u'\u0147', u'\u00D3', u'\u00D4', u'\u0150', u'\u00D6', u'\u00D7',
    u'\u0158', u'\u016E', u'\u00DA', u'\u0170', u'\u00DC', u'\u00DD', u'\u0162', u'\u00DF',
    u'\u0155', u'\u00E1', u'\u00E2', u'\u0103', u'\u00E4', u'\u013A', u'\u0107', u'\u00E7',
    u'\u010D', u'\u00E9', u'\u0119', u'\u00EB', u'\u011B', u'\u00ED', u'\u00EE', u'\u010F',
    u'\u0111', u'\u0144', u'\u0148', u'\u00F3', u'\u00F4', u'\u0151', u'\u00F6', u'\u00F7',
    u'\u0159', u'\u016F', u'\u00FA', u'\u0171', u'\u00FC', u'\u00FD', u'\u0163', u'\u02D9',
};

constexpr std::array<char16_t, 128> kWindows1251High{
    u'\u0402', u'\u0403', u'\u201A', u'\u0453', u'\u201E', u'\u2026', u'\u2020', u'\u2021',
    u'\u20AC', u'\u2030', u'\u0409', u'\u2039', u'\u040A', u'\u040C', u'\u040B', u'\u040F',
    u'\u0452', u'\u2018', u'\u2019', u'\u201C', u'\u201D', u'\u2022', u'\u2013', u'\u2014',
    u'\u0098', u'\u2122', u'\u0459', u'\u203A', u'\u045A', u'\u045C', u'\u045B', u'\u045F',
    u'\u00A0', u'\u040E', u'\u045E', u'\u0408', u'\u00A4', u'\u0490', u'\u00A6', u'\u00A7',
    u'\u0401', u'\u00A9', u'\u0404', u'\u00AB', u'\u00AC', u'\u00AD', u'\u00AE', u'\u0407',
    u'\u00B0', u'\u00B1', u'\u0406', u'\u0456', u'\u0491', u'\u00B5', u'\u00B6', u'\u00B7',
    u'\u0451', u'\u2116', u'\u0454', u'\u00BB', u'\u0458', u'\u0405', u'\u0455', u'\u0457',
    u'\u0410', u'\u0411', u'\u0412', u'\u0413', u'\u0414', u'\u0415', u'\u0416', u'\u0417',
    u'\u0418', u'\u0419', u'\u041A', u'\u041B', u'\u041C', u'\u041D', u'\u041E', u'\u041F',
    u'\u0420', u'\u0421', u'\u0422', u'\u0423', u'\u0424', u'\u0425', u'\u0426', u'\u0427',
    u'\u0428', u'\u0429', u'\u042A', u'\u042B', u'\u042C', u'\u042D', u'\u042E', u'\u042F',
    u'\u0430', u'\u0431', u'\u0432', u'\u0433', u'\u0434', u'\u0435', u'\u0436', u'\u0437',
    u'\u0438', u'\u0439', u'\u043A', u'\u043B', u'\u043C', u'\u043D', u'\u043E', u'\u043F',
    u'\u0440', u'\u0441', u'\u0442', u'\u0443', u'\u0444', u'\u0445', u'\u0446', u'\u0447',
    u'\u0448', u'\u0449', u'\u044A', u'\u044B', u'\u044C', u'\u044D', u'\u044E', u'\u044F',
};

constexpr std::array<char16_t, 128> kIso885915High{
    u'\u0080', u'\u0081', u'\u0082', u'\u0083', u'\u0084', u'\u0085', u'\u0086', u'\u0087',
    u'\u0088', u'\u0089', u'\u008A', u'\u008B', u'\u008C', u'\u008D', u'\u008E', u'\u008F',
    u'\u0090', u'\u0091', u'\u0092', u'\u0093', u'\u0094', u'\u0095', u'\u0096', u'\u0097',
    u'\u0098', u'\u0099', u'\u009A', u'\u009B', u'\u009C', u'\u009D', u'\u009E', u'\u009F',
    u'\u00A0', u'\u00A1', u'\u00A2', u'\u00A3', u'\u20AC', u'\u00A5', u'\u0160', u'\u00A7',
    u'\u0161', u'\u00A9', u'\u00AA', u'\u00AB', u'\u00AC', u'\u00AD', u'\u00AE', u'\u00AF',
    u'\u00B0', u'\u00B1', u'\u00B2', u'\u00B3', u'\u017D', u'\u00B5', u'\u00B6', u'\u00B7',
    u'\u017E', u'\u00B9', u'\u00BA', u'\u00BB', u'\u0152', u'\u0153', u'\u0178', u'\u00BF',
    u'\u00C0', u'\u00C1', u'\u00C2', u'\u00C3', u'\u00C4', u'\u00C5', u'\u00C6', u'\u00C7',
    u'\u00C8', u'\u00C9', u'\u00CA', u'\u00CB', u'\u00CC', u'\u00CD', u'\u00CE', u'\u00CF',
    u'\u00D0', u'\u00D1', u'\u00D2', u'\u00D3', u'\u00D4', u'\u00D5', u'\u00D6', u'\u00D7',
    u'\u00D8', u'\u00D9', u'\u00DA', u'\u00DB', u'\u00DC', u'\u00DD', u'\u00DE', u'\u00DF',
    u'\u00E0', u'\u00E1', u'\u00E2', u'\u00E3', u'\u00E4', u'\u00E5', u'\u00E6', u'\u00E7',
    u'\u00E8', u'\u00E9', u'\u00EA', u'\u00EB', u'\u00EC', u'\u00ED', u'\u00EE', u'\u00EF',
    u'\u00F0', u'\u00F1', u'\u00F2', u'\u00F3', u'\u00F4', u'\u00F5', u'\u00F6', u'\u00F7',
    u'\u00F8', u'\u00F9', u'\u00FA', u'\u00FB', u'\u00FC', u'\u00FD', u'\u00FE', u'\u00FF',
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

// Code pages whose upper half (0x80..0xFF) comes from a table; ASCII stays as is.
void decodeTable(std::span<const std::byte> raw, const std::array<char16_t, 128>& high, Output& out)
{
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const unsigned value = byteAt(raw, i);
        out.codePoint(value < 0x80U ? value : high.at(value - 0x80U), i);
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
    case Encoding::kWindows1250:
        decodeTable(raw, kWindows1250High, output);
        break;
    case Encoding::kWindows1251:
        decodeTable(raw, kWindows1251High, output);
        break;
    case Encoding::kIso885915:
        decodeTable(raw, kIso885915High, output);
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
