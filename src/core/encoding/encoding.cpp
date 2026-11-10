#include "core/encoding/encoding.h"

#include <simdutf.h>

#include <array>

namespace lv::core
{

namespace
{

constexpr std::array<std::byte, 1> kLf8{std::byte{0x0A}};
constexpr std::array<std::byte, 1> kCr8{std::byte{0x0D}};
constexpr std::array<std::byte, 2> kLf16Le{std::byte{0x0A}, std::byte{0x00}};
constexpr std::array<std::byte, 2> kCr16Le{std::byte{0x0D}, std::byte{0x00}};
constexpr std::array<std::byte, 2> kLf16Be{std::byte{0x00}, std::byte{0x0A}};
constexpr std::array<std::byte, 2> kCr16Be{std::byte{0x00}, std::byte{0x0D}};
constexpr std::array<std::byte, 3> kBomUtf8{std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF}};
constexpr std::array<std::byte, 2> kBomUtf16Le{std::byte{0xFF}, std::byte{0xFE}};
constexpr std::array<std::byte, 2> kBomUtf16Be{std::byte{0xFE}, std::byte{0xFF}};

const EncodingTraits kUtf8Traits{
    .codeUnitSize = 1, .lineFeed = kLf8, .carriageReturn = kCr8, .byteOrderMark = kBomUtf8};
const EncodingTraits kUtf16LeTraits{.codeUnitSize = 2,
                                    .lineFeed = kLf16Le,
                                    .carriageReturn = kCr16Le,
                                    .byteOrderMark = kBomUtf16Le};
const EncodingTraits kUtf16BeTraits{.codeUnitSize = 2,
                                    .lineFeed = kLf16Be,
                                    .carriageReturn = kCr16Be,
                                    .byteOrderMark = kBomUtf16Be};
const EncodingTraits kSingleByteTraits{
    .codeUnitSize = 1, .lineFeed = kLf8, .carriageReturn = kCr8, .byteOrderMark = {}};

struct NameEntry
{
    Encoding encoding;
    std::string_view name;
};

constexpr std::array<NameEntry, 8> kNames{{
    {.encoding = Encoding::kUtf8, .name = "UTF-8"},
    {.encoding = Encoding::kUtf16Le, .name = "UTF-16 LE"},
    {.encoding = Encoding::kUtf16Be, .name = "UTF-16 BE"},
    {.encoding = Encoding::kWindows1252, .name = "Windows-1252"},
    {.encoding = Encoding::kIso88591, .name = "ISO-8859-1"},
    {.encoding = Encoding::kWindows1250, .name = "Windows-1250"},
    {.encoding = Encoding::kWindows1251, .name = "Windows-1251"},
    {.encoding = Encoding::kIso885915, .name = "ISO-8859-15"},
}};

bool startsWith(std::span<const std::byte> data, std::span<const std::byte> prefix) noexcept
{
    if (data.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (data[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

// Length of the incomplete UTF-8 sequence at the end of `data` (0 if it ends on a boundary).
// Only checks the structure of the last up to 3 bytes; validity is checked separately.
std::size_t incompleteTailLength(std::span<const std::byte> data) noexcept
{
    const std::size_t maxBack = data.size() < 3 ? data.size() : 3;
    for (std::size_t back = 1; back <= maxBack; ++back) {
        const auto byte = std::to_integer<unsigned>(data[data.size() - back]);
        if ((byte & 0xC0U) == 0x80U) {
            continue;  // continuation byte, keep looking for the lead byte
        }
        std::size_t expected = 1;
        if ((byte & 0xE0U) == 0xC0U) {
            expected = 2;
        } else if ((byte & 0xF0U) == 0xE0U) {
            expected = 3;
        } else if ((byte & 0xF8U) == 0xF0U) {
            expected = 4;
        }
        return expected > back ? back : 0;
    }
    return 0;
}

}  // namespace

const EncodingTraits& traits(Encoding encoding) noexcept
{
    switch (encoding) {
    case Encoding::kUtf8:
        return kUtf8Traits;
    case Encoding::kUtf16Le:
        return kUtf16LeTraits;
    case Encoding::kUtf16Be:
        return kUtf16BeTraits;
    case Encoding::kWindows1252:
    case Encoding::kIso88591:
    case Encoding::kWindows1250:
    case Encoding::kWindows1251:
    case Encoding::kIso885915:
        return kSingleByteTraits;
    }
    return kUtf8Traits;
}

std::string_view displayName(Encoding encoding) noexcept
{
    for (const auto& entry : kNames) {
        if (entry.encoding == encoding) {
            return entry.name;
        }
    }
    return "Unknown";
}

std::optional<Encoding> encodingFromName(std::string_view name) noexcept
{
    for (const auto& entry : kNames) {
        if (entry.name == name) {
            return entry.encoding;
        }
    }
    return std::nullopt;
}

DetectionResult detectEncoding(std::span<const std::byte> sample, bool atEndOfFile) noexcept
{
    if (startsWith(sample, kBomUtf8)) {
        return {.encoding = Encoding::kUtf8, .bomLength = kBomUtf8.size()};
    }
    if (startsWith(sample, kBomUtf16Le)) {
        return {.encoding = Encoding::kUtf16Le, .bomLength = kBomUtf16Le.size()};
    }
    if (startsWith(sample, kBomUtf16Be)) {
        return {.encoding = Encoding::kUtf16Be, .bomLength = kBomUtf16Be.size()};
    }
    std::size_t length = sample.size();
    if (!atEndOfFile) {
        length -= incompleteTailLength(sample);
    }
    const bool valid = simdutf::validate_utf8(reinterpret_cast<const char*>(sample.data()), length);
    return {.encoding = valid ? Encoding::kUtf8 : Encoding::kWindows1252, .bomLength = 0};
}

}  // namespace lv::core
