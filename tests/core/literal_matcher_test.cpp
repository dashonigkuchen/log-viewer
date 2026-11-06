#include "core/search/literal_matcher.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <random>
#include <string>
#include <string_view>
#include <vector>

using lv::core::LiteralMatcher;
using lv::core::LiteralOptions;
using lv::core::MatchRange;
using lv::core::MatchScratch;

namespace
{

bool matches(std::string pattern, std::string_view line, LiteralOptions options = {})
{
    MatchScratch scratch;
    const LiteralMatcher matcher(std::move(pattern), options);
    return matcher.matches(line, scratch);
}

std::vector<MatchRange> ranges(std::string pattern, std::string_view line,
                               LiteralOptions options = {})
{
    MatchScratch scratch;
    const LiteralMatcher matcher(std::move(pattern), options);
    std::vector<MatchRange> out;
    matcher.collectRanges(line, scratch, out);
    return out;
}

constexpr LiteralOptions kSensitive{.caseSensitive = true, .wholeWord = false};
constexpr LiteralOptions kWord{.caseSensitive = false, .wholeWord = true};

}  // namespace

TEST_CASE("literal matcher table", "[search][literal]")
{
    struct Row
    {
        const char* pattern;
        const char* line;
        LiteralOptions options;
        bool expected;
    };

    const auto row = GENERATE(values<Row>({
        {"ERROR", "2026 ERROR db down", kSensitive, true},
        {"error", "2026 ERROR db down", kSensitive, false},
        {"error", "2026 ERROR db down", {}, true},
        {"ErRoR", "2026 error db down", {}, true},
        {"down", "2026 ERROR db down", kSensitive, true},  // at the end
        {"2026", "2026 ERROR db down", kSensitive, true},  // at the start
        {"x", "", {}, false},
        {"longer than the line", "short", {}, false},
        {"short", "short", kSensitive, true},
        {"aab", "aaab", {}, true},    // retry after a partial match
        {"ÜBER", "über", {}, false},  // non-ASCII is not folded here (M3-04 uses regex)
        {"über", "Text über alles", kSensitive, true},
        {"err", "error", kWord, false},
        {"error", "an error.", kWord, true},
        {"error", "error_code", kWord, false},  // underscore is a word char
        {"error", "error2", kWord, false},      // digits are word chars
        {"error", "2error", kWord, false},
        {"error", "errors error", kWord, true},  // second occurrence qualifies
        {"Größe", "die Größe", kWord, true},
        {"Gr", "Größe", kWord, false},    // non-ASCII byte after the match is a word char
        {"[x]", "a [X] b", kWord, true},  // non-word pattern edges
    }));
    CAPTURE(row.pattern, row.line, row.options.caseSensitive, row.options.wholeWord);
    CHECK(matches(row.pattern, row.line, row.options) == row.expected);
}

TEST_CASE("empty pattern matches everything without ranges", "[search][literal]")
{
    CHECK(matches("", ""));
    CHECK(matches("", "anything"));
    CHECK(matches("", "anything", kWord));
    CHECK(ranges("", "anything").empty());
}

TEST_CASE("collectRanges returns non-overlapping matches", "[search][literal]")
{
    CHECK(ranges("aa", "aaaaa") == std::vector<MatchRange>{{0, 2}, {2, 4}});
    CHECK(ranges("ab", "xABxabx") == std::vector<MatchRange>{{1, 3}, {4, 6}});
    CHECK(ranges("ab", "xABxabx", kSensitive) == std::vector<MatchRange>{{4, 6}});
    CHECK(ranges("id", "id idx id_ id", kWord) == std::vector<MatchRange>{{0, 2}, {11, 13}});
    CHECK(ranges("zzz", "abc").empty());
}

TEST_CASE("ascii detection", "[search][literal]")
{
    CHECK(LiteralMatcher::isAscii("plain text 123"));
    CHECK_FALSE(LiteralMatcher::isAscii("Größe"));
}

TEST_CASE("case-insensitive search agrees with a naive reference", "[search][literal]")
{
    std::mt19937 rng(7);
    const std::string alphabet = "aAbB_ 1";
    auto randomText = [&](std::size_t length) {
        std::string text;
        for (std::size_t i = 0; i < length; ++i) {
            text += alphabet[rng() % alphabet.size()];
        }
        return text;
    };
    auto lower = [](std::string text) {
        for (char& c : text) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return text;
    };
    for (int i = 0; i < 2000; ++i) {
        const std::string pattern = randomText(1 + (rng() % 3));
        const std::string line = randomText(rng() % 20);
        const bool expected = lower(line).find(lower(pattern)) != std::string::npos;
        CAPTURE(pattern, line);
        REQUIRE(matches(pattern, line) == expected);
    }
}

TEST_CASE("scratch keeps one entry per owner", "[search][literal]")
{
    struct Data : lv::core::ScratchData
    {
        int value = 0;
    };

    MatchScratch scratch;
    int creations = 0;
    const int ownerA = 0;
    const int ownerB = 0;
    auto create = [&] {
        ++creations;
        return std::make_unique<Data>();
    };
    scratch.get<Data>(&ownerA, create).value = 1;
    scratch.get<Data>(&ownerB, create).value = 2;
    CHECK(scratch.get<Data>(&ownerA, create).value == 1);
    CHECK(scratch.get<Data>(&ownerB, create).value == 2);
    CHECK(creations == 2);
}
