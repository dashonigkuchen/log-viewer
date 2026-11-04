#pragma once

#include "core/encoding/encoding.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Decoding raw line bytes for display and mapping between byte offsets, UTF-16 offsets, and
// display columns (ARCHITECTURE §3.2, SPEC F-ENC-4, F-VIEW-4, F-ROB-1).
namespace lv::core
{

inline constexpr char16_t kReplacementCharacter = u'\uFFFD';

struct DecodeOptions
{
    // Replace C0 control characters (except tab) and DEL by their visible "Control Pictures"
    // (U+2400..U+2421), e.g. NUL -> U+2400. Used for display; search decodes without it.
    bool visibleControls = false;
};

// Decodes `raw` (one line without its terminator) into UTF-16.
// Invalid UTF-8 sequences become U+FFFD (one per maximal invalid subpart), unpaired UTF-16
// surrogates and a trailing odd byte become U+FFFD; never fails.
// If `sourceOffsets` is given, it receives one entry per produced UTF-16 code unit with the byte
// offset in `raw` where that unit's character starts, plus a final entry equal to raw.size().
void decodeLine(std::span<const std::byte> raw, Encoding encoding, std::u16string& out,
                std::vector<std::uint32_t>* sourceOffsets = nullptr, DecodeOptions options = {});

// Convenience overload returning the text.
[[nodiscard]] std::u16string decodeLine(std::span<const std::byte> raw, Encoding encoding,
                                        DecodeOptions options = {});

// UTF-16 offset of the code unit that starts at or covers `byteOffset`, given the source offsets
// produced by decodeLine. Byte offsets inside a multi-byte character map to that character.
[[nodiscard]] std::size_t utf16OffsetForByte(std::span<const std::uint32_t> sourceOffsets,
                                             std::uint32_t byteOffset) noexcept;

// Byte offset in the raw line where the character of UTF-16 unit `utf16Offset` starts
// (utf16Offset == text size maps to raw.size()).
[[nodiscard]] std::uint32_t byteOffsetForUtf16(std::span<const std::uint32_t> sourceOffsets,
                                               std::size_t utf16Offset) noexcept;

// Display columns: every character takes one column, a tab advances to the next multiple of
// `tabWidth`. Returns one start column per UTF-16 unit plus a final entry with the total width;
// the low half of a surrogate pair shares the column of its high half.
[[nodiscard]] std::vector<std::uint32_t> displayColumns(std::u16string_view text,
                                                        std::uint32_t tabWidth);

// Replaces tabs by spaces up to the next tab stop.
[[nodiscard]] std::u16string expandTabs(std::u16string_view text, std::uint32_t tabWidth);

}  // namespace lv::core
