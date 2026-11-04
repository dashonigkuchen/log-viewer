#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// Text encodings of log files (SPEC F-ENC-1, ARCHITECTURE §3.2).
namespace lv::core
{

enum class Encoding : std::uint8_t
{
    kUtf8,
    kUtf16Le,
    kUtf16Be,
    kWindows1252,
    kIso88591,
};

// Static properties of an encoding that the indexer and the decoder need.
struct EncodingTraits
{
    // Size of one code unit in bytes: 1 or 2.
    std::size_t codeUnitSize = 1;
    // Byte representation of the line feed and carriage return code units.
    std::span<const std::byte> lineFeed;
    std::span<const std::byte> carriageReturn;
    // Byte order mark, empty if the encoding has none.
    std::span<const std::byte> byteOrderMark;
};

[[nodiscard]] const EncodingTraits& traits(Encoding encoding) noexcept;

// Stable English name, e.g. "UTF-8", "UTF-16 LE", "Windows-1252" (shown in the status bar).
[[nodiscard]] std::string_view displayName(Encoding encoding) noexcept;

// Inverse of displayName (exact match), for settings and command line.
[[nodiscard]] std::optional<Encoding> encodingFromName(std::string_view name) noexcept;

// Number of bytes to sample from the start of a file for detection.
inline constexpr std::size_t kDetectionSampleSize = std::size_t{1024} * 1024;

struct DetectionResult
{
    Encoding encoding = Encoding::kUtf8;
    // Length of the byte order mark at the start of the file (0, 2, or 3). The indexer skips it.
    std::size_t bomLength = 0;
};

// Detects the encoding from the first bytes of a file (F-ENC-2):
// 1. a byte order mark (UTF-8, UTF-16 LE, UTF-16 BE);
// 2. otherwise UTF-8 if the sample is valid UTF-8 (a sequence cut off at the end of the sample
//    counts as valid, because the sample may end in the middle of a character);
// 3. otherwise Windows-1252.
// `sample` is normally the first kDetectionSampleSize bytes; pass `atEndOfFile` = true when the
// sample contains the whole file, so a truncated sequence at the end is treated as invalid.
[[nodiscard]] DetectionResult detectEncoding(std::span<const std::byte> sample,
                                             bool atEndOfFile = false) noexcept;

}  // namespace lv::core
