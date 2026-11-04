#include "core/encoding/encoding.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using lv::core::detectEncoding;
using lv::core::Encoding;

namespace
{

std::span<const std::byte> bytes(std::string_view text)
{
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

std::string asString(std::span<const std::byte> data)
{
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

}  // namespace

TEST_CASE("byte order marks are detected", "[core][encoding]")
{
    const auto utf8 = detectEncoding(bytes("\xEF\xBB\xBFhello"));
    CHECK(utf8.encoding == Encoding::kUtf8);
    CHECK(utf8.bomLength == 3);

    const auto le = detectEncoding(bytes(std::string_view("\xFF\xFEh\0i\0", 6)));
    CHECK(le.encoding == Encoding::kUtf16Le);
    CHECK(le.bomLength == 2);

    const auto be = detectEncoding(bytes(std::string_view("\xFE\xFF\0h\0i", 6)));
    CHECK(be.encoding == Encoding::kUtf16Be);
    CHECK(be.bomLength == 2);
}

TEST_CASE("text without BOM", "[core][encoding]")
{
    CHECK(detectEncoding(bytes("")).encoding == Encoding::kUtf8);
    CHECK(detectEncoding(bytes("plain ASCII log line\n")).encoding == Encoding::kUtf8);
    CHECK(detectEncoding(bytes("Gr\xC3\xB6\xC3\x9F"
                               "e \xE2\x82\xAC \xF0\x9F\x98\x80\n"))
              .encoding == Encoding::kUtf8);
    CHECK(detectEncoding(bytes("Gr\xF6\xDF"
                               "e\n"))
              .encoding == Encoding::kWindows1252);
    CHECK(detectEncoding(bytes("ok \xC3\x28 broken")).encoding == Encoding::kWindows1252);
    CHECK(detectEncoding(bytes("overlong \xC0\xAF")).encoding == Encoding::kWindows1252);
    CHECK(detectEncoding(bytes("")).bomLength == 0);
}

TEST_CASE("a UTF-8 sequence cut at the end of the sample is still UTF-8", "[core][encoding]")
{
    for (const std::string_view character : {"\xC3\xBC", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"}) {
        for (std::size_t cut = 1; cut < character.size(); ++cut) {
            const std::string sample = "abc " + std::string(character.substr(0, cut));
            INFO("character bytes: " << character.size() << ", cut after " << cut);
            CHECK(detectEncoding(bytes(sample)).encoding == Encoding::kUtf8);
            // When the sample is the whole file, the truncated sequence is invalid.
            CHECK(detectEncoding(bytes(sample), true).encoding == Encoding::kWindows1252);
        }
    }
}

TEST_CASE("invalid bytes before the cut are still detected", "[core][encoding]")
{
    CHECK(detectEncoding(bytes("abc \xFF \xC3")).encoding == Encoding::kWindows1252);
    CHECK(detectEncoding(bytes("abc \x80")).encoding == Encoding::kWindows1252);
}

TEST_CASE("encoding traits", "[core][encoding]")
{
    const auto& utf8 = lv::core::traits(Encoding::kUtf8);
    CHECK(utf8.codeUnitSize == 1);
    CHECK(asString(utf8.lineFeed) == "\n");
    CHECK(asString(utf8.carriageReturn) == "\r");
    CHECK(asString(utf8.byteOrderMark) == "\xEF\xBB\xBF");

    const auto& le = lv::core::traits(Encoding::kUtf16Le);
    CHECK(le.codeUnitSize == 2);
    CHECK(asString(le.lineFeed) == std::string("\n\0", 2));
    CHECK(asString(le.carriageReturn) == std::string("\r\0", 2));

    const auto& be = lv::core::traits(Encoding::kUtf16Be);
    CHECK(asString(be.lineFeed) == std::string("\0\n", 2));
    CHECK(asString(be.byteOrderMark) == "\xFE\xFF");

    CHECK(lv::core::traits(Encoding::kWindows1252).byteOrderMark.empty());
    CHECK(lv::core::traits(Encoding::kIso88591).codeUnitSize == 1);
}

TEST_CASE("encoding names round-trip", "[core][encoding]")
{
    for (const Encoding encoding : {Encoding::kUtf8, Encoding::kUtf16Le, Encoding::kUtf16Be,
                                    Encoding::kWindows1252, Encoding::kIso88591}) {
        CHECK(lv::core::encodingFromName(lv::core::displayName(encoding)) == encoding);
    }
    CHECK(lv::core::displayName(Encoding::kUtf16Le) == "UTF-16 LE");
    CHECK_FALSE(lv::core::encodingFromName("utf-8").has_value());
}

TEST_CASE("detection on a full 1 MiB sample", "[core][encoding]")
{
    std::string sample;
    while (sample.size() < lv::core::kDetectionSampleSize) {
        sample += "2026-01-01 INFO Größe geändert\n";
    }
    sample.resize(lv::core::kDetectionSampleSize);  // may cut a multi-byte character
    CHECK(detectEncoding(bytes(sample)).encoding == Encoding::kUtf8);
    sample[1000] = '\xFF';
    CHECK(detectEncoding(bytes(sample)).encoding == Encoding::kWindows1252);
}
