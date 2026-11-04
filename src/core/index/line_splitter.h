#pragma once

#include "core/encoding/encoding.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace lv::core
{

// Finds line terminators in a stream of file data that arrives in buffers of any size
// (ARCHITECTURE §3.4, SPEC F-FILE-5).
//
// For every line feed code unit it reports the line *end offset*: the file offset just after the
// line feed. "\r\n" needs no special handling here: the line feed ends the line, and readers drop
// a carriage return directly before it when they extract the line text. A lone "\r" is not a
// line break. The unterminated last line is not reported (the line index tracks it separately).
//
// For UTF-16, only line feed code units at code-unit boundaries count; buffers may split a code
// unit (odd buffer sizes), the splitter carries the odd byte over.
//
// Thread-safety: none; one splitter per indexing run.
class LineSplitter
{
public:
    // `startOffset` is the file offset of the first byte that will be fed. For UTF-16 it must be
    // at a code-unit boundary (after the byte order mark, or any even distance from it).
    LineSplitter(Encoding encoding, std::uint64_t startOffset) noexcept;

    // Scans `data` (the bytes at offset position()) and appends line end offsets to `lineEnds`.
    void feed(std::span<const std::byte> data, std::vector<std::uint64_t>& lineEnds);

    // File offset just after the last fed byte.
    [[nodiscard]] std::uint64_t position() const noexcept { return m_position; }

private:
    void feedSingleByte(std::span<const std::byte> data, std::vector<std::uint64_t>& lineEnds);
    void feedUtf16(std::span<const std::byte> data, std::vector<std::uint64_t>& lineEnds);

    bool m_utf16 = false;
    bool m_bigEndian = false;
    std::uint64_t m_position = 0;
    // UTF-16: first byte of a code unit whose second byte is in the next buffer.
    std::optional<std::byte> m_carry;
};

}  // namespace lv::core
