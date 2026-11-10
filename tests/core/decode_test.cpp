#include "core/encoding/decode.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using lv::core::decodeLine;
using lv::core::DecodeOptions;
using lv::core::Encoding;

namespace
{

std::span<const std::byte> bytes(std::string_view text)
{
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

std::u16string decode(std::string_view raw, Encoding encoding, DecodeOptions options = {})
{
    return decodeLine(bytes(raw), encoding, options);
}

}  // namespace

TEST_CASE("UTF-8 decoding", "[core][encoding]")
{
    CHECK(decode("", Encoding::kUtf8).empty());
    CHECK(decode("plain", Encoding::kUtf8) == u"plain");
    CHECK(decode("Gr\xC3\xB6\xC3\x9F"
                 "e \xE2\x82\xAC",
                 Encoding::kUtf8) == u"Größe €");
    CHECK(decode("\xF0\x9F\x98\x80", Encoding::kUtf8) == u"\U0001F600");
}

TEST_CASE("invalid UTF-8 becomes U+FFFD (F-ENC-4)", "[core][encoding]")
{
    CHECK(decode("\xFF", Encoding::kUtf8) == u"\uFFFD");
    CHECK(decode("a\x80"
                 "b",
                 Encoding::kUtf8) == u"a\uFFFDb");
    CHECK(decode("\xC3", Encoding::kUtf8) == u"\uFFFD");      // truncated at the end
    CHECK(decode("\xE2\x82", Encoding::kUtf8) == u"\uFFFD");  // one replacement
    CHECK(decode("\xE2\x82"
                 "x",
                 Encoding::kUtf8) == u"\uFFFDx");
    CHECK(decode("\xC0\xAF", Encoding::kUtf8) == u"\uFFFD\uFFFD");            // overlong lead
    CHECK(decode("\xED\xA0\x80", Encoding::kUtf8) == u"\uFFFD\uFFFD\uFFFD");  // surrogate
    CHECK(decode("\xF4\x90\x80\x80", Encoding::kUtf8) ==
          u"\uFFFD\uFFFD\uFFFD\uFFFD");  // > U+10FFFF
    CHECK(decode("\xF0\x9F\x98"
                 "!",
                 Encoding::kUtf8) == u"\uFFFD!");
    CHECK(decode("\xC3\xBC\xFF\xC3\xBC", Encoding::kUtf8) == u"ü\uFFFDü");
}

TEST_CASE("single-byte code pages", "[core][encoding]")
{
    CHECK(decode("Gr\xF6\xDF"
                 "e",
                 Encoding::kWindows1252) == u"Größe");
    CHECK(decode("\x80 \x93x\x94 \x81", Encoding::kWindows1252) == u"€ “x” \u0081");
    CHECK(decode("\x80\xA4", Encoding::kIso88591) == u"\u0080\u00A4");
}

TEST_CASE("UTF-16 decoding", "[core][encoding]")
{
    CHECK(decode(std::string_view("a\0\xFC\0", 4), Encoding::kUtf16Le) == u"aü");
    CHECK(decode(std::string_view("\0a\0\xFC", 4), Encoding::kUtf16Be) == u"aü");
    CHECK(decode(std::string_view("\x3D\xD8\x00\xDE", 4), Encoding::kUtf16Le) == u"\U0001F600");
    CHECK(decode(std::string_view("\x3D\xD8"
                                  "a\0",
                                  4),
                 Encoding::kUtf16Le) == u"\uFFFDa");                                  // lone high
    CHECK(decode(std::string_view("\x00\xDE", 2), Encoding::kUtf16Le) == u"\uFFFD");  // lone low
    CHECK(decode(std::string_view("a\0b", 3), Encoding::kUtf16Le) == u"a\uFFFD");     // odd byte
    CHECK(decode(std::string_view("\x3D\xD8", 2), Encoding::kUtf16Le) == u"\uFFFD");  // high at end
}

TEST_CASE("visible control characters (F-ROB-1)", "[core][encoding]")
{
    const DecodeOptions visible{.visibleControls = true};
    CHECK(decode(std::string_view("a\0b\x1B\x7F\tc", 7), Encoding::kUtf8, visible) ==
          u"a\u2400b\u241B\u2421\tc");
    CHECK(decode(std::string_view("a\0b", 3), Encoding::kUtf8) == std::u16string(u"a\0b", 3));
}

TEST_CASE("byte and UTF-16 offsets map both ways", "[core][encoding]")
{
    const std::string raw = "a\xC3\xBC\xF0\x9F\x98\x80"
                            "b\xE2\x82\xAC";
    std::u16string text;
    std::vector<std::uint32_t> offsets;
    decodeLine(bytes(raw), Encoding::kUtf8, text, &offsets);
    REQUIRE(text == u"aü\U0001F600b€");
    CHECK(offsets == std::vector<std::uint32_t>{0, 1, 3, 3, 7, 8, 11});
    CHECK(lv::core::utf16OffsetForByte(offsets, 0) == 0);
    CHECK(lv::core::utf16OffsetForByte(offsets, 2) == 1);  // inside ü
    CHECK(lv::core::utf16OffsetForByte(offsets, 5) == 2);  // inside the emoji -> high surrogate
    CHECK(lv::core::utf16OffsetForByte(offsets, 7) == 4);
    CHECK(lv::core::utf16OffsetForByte(offsets, 11) == 6);  // end
    CHECK(lv::core::byteOffsetForUtf16(offsets, 4) == 7);
    CHECK(lv::core::byteOffsetForUtf16(offsets, 6) == 11);
}

TEST_CASE("offset mapping round-trips for random input", "[core][encoding]")
{
    const auto encoding =
        GENERATE(Encoding::kUtf8, Encoding::kUtf16Le, Encoding::kUtf16Be, Encoding::kWindows1252);
    std::mt19937_64 random(5);
    for (int round = 0; round < 200; ++round) {
        std::string raw(random() % 64, '\0');
        for (auto& c : raw) {
            c = static_cast<char>(random() % 256);
        }
        std::u16string text;
        std::vector<std::uint32_t> offsets;
        decodeLine(bytes(raw), encoding, text, &offsets);
        REQUIRE(offsets.size() == text.size() + 1);
        REQUIRE(offsets.back() == raw.size());
        REQUIRE(std::ranges::is_sorted(offsets));
        for (std::size_t unit = 0; unit < text.size(); ++unit) {
            const std::uint32_t byte = lv::core::byteOffsetForUtf16(offsets, unit);
            const std::size_t back = lv::core::utf16OffsetForByte(offsets, byte);
            // Back maps to the first unit of the same character.
            REQUIRE(back <= unit);
            REQUIRE(offsets[back] == byte);
        }
    }
}

TEST_CASE("tabs and display columns (F-VIEW-4)", "[core][encoding]")
{
    CHECK(lv::core::expandTabs(u"a\tb", 8) == u"a       b");
    CHECK(lv::core::expandTabs(u"\t", 4) == u"    ");
    CHECK(lv::core::expandTabs(u"abcd\te", 4) == u"abcd    e");
    CHECK(lv::core::expandTabs(u"\U0001F600\tx", 4) == u"\U0001F600   x");
    CHECK(lv::core::displayColumns(u"a\tb", 4) == std::vector<std::uint32_t>{0, 1, 4, 5});
    CHECK(lv::core::displayColumns(u"\U0001F600x", 8) == std::vector<std::uint32_t>{0, 0, 1, 2});
    CHECK(lv::core::displayColumns(u"", 8) == std::vector<std::uint32_t>{0});
}

TEST_CASE("additional single-byte code pages", "[core][decode]")
{
    // F-ENC-5. Reference strings from the Unicode mapping tables.
    SECTION("Windows-1250 (Central European)")
    {
        // "Příliš žluťoučký kůň" and the Polish "Łódź".
        CHECK(decode("P\xF8\xEDli\x9A \x9Elu\x9Dou\xE8k\xFD k\xF9\xF2", Encoding::kWindows1250) ==
              u"P\u0159\u00EDli\u0161 \u017Elu\u0165ou\u010Dk\u00FD k\u016F\u0148");
        CHECK(decode("\xA3\xF3\x64\x9F", Encoding::kWindows1250) == u"\u0141\u00F3d\u017A");
        CHECK(decode("\x80 \x8A\x8C", Encoding::kWindows1250) == u"\u20AC \u0160\u015A");
        CHECK(decode("\x81", Encoding::kWindows1250) == u"\u0081");  // undefined byte
    }
    SECTION("Windows-1251 (Cyrillic)")
    {
        CHECK(decode("\xCF\xF0\xE8\xE2\xE5\xF2, \xEC\xE8\xF0!", Encoding::kWindows1251) ==
              u"\u041F\u0440\u0438\u0432\u0435\u0442, \u043C\u0438\u0440!");
        CHECK(decode("\xA8\xB8 \x80 \xB9", Encoding::kWindows1251) ==
              u"\u0401\u0451 \u0402 \u2116");
        CHECK(decode("\x98", Encoding::kWindows1251) == u"\u0098");  // undefined byte
    }
    SECTION("ISO-8859-15 (Latin-9)")
    {
        CHECK(decode("\xA4 \xA6\xA8 \xB4\xB8 \xBC\xBD\xBE", Encoding::kIso885915) ==
              u"\u20AC \u0160\u0161 \u017D\u017E \u0152\u0153\u0178");
        CHECK(decode("Gr\xF6\xDF\x65", Encoding::kIso885915) == u"Gr\u00F6\u00DFe");
        CHECK(decode("\x80", Encoding::kIso885915) == u"\u0080");  // C1 as in ISO-8859-1
    }
    SECTION("ASCII is unchanged")
    {
        for (const Encoding encoding :
             {Encoding::kWindows1250, Encoding::kWindows1251, Encoding::kIso885915}) {
            CHECK(decode("plain ASCII 123 \t~", encoding) == u"plain ASCII 123 \t~");
            CHECK(lv::core::traits(encoding).codeUnitSize == 1);
        }
    }
}
