#include "core/search/regex_matcher.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using lv::core::MatchRange;
using lv::core::MatchScratch;
using lv::core::RegexMatcher;
using lv::core::RegexOptions;
using lv::core::RegexOutcome;

namespace
{

std::unique_ptr<RegexMatcher> compile(std::string_view pattern, RegexOptions options = {})
{
    auto result = RegexMatcher::compile(pattern, options);
    REQUIRE(result.has_value());
    return std::move(*result);
}

constexpr RegexOptions kSensitive{.caseSensitive = true};

}  // namespace

TEST_CASE("regex matcher on common log patterns", "[search][regex]")
{
    struct Row
    {
        const char* pattern;
        const char* line;
        bool caseSensitive;
        bool expected;
    };

    const auto row = GENERATE(values<Row>({
        {R"(ERROR|FATAL)", "2026-11-06 FATAL disk full", true, true},
        {R"(ERROR|FATAL)", "2026-11-06 fatal disk full", true, false},
        {R"(ERROR|FATAL)", "2026-11-06 fatal disk full", false, true},
        {R"(^\d{4}-\d{2}-\d{2} )", "2026-11-06 INFO", true, true},
        {R"(^\d{4}-\d{2}-\d{2} )", "INFO 2026-11-06 ", true, false},
        {R"(duration=(\d{4,})ms)", "req duration=1500ms", true, true},
        {R"(duration=(\d{4,})ms)", "req duration=150ms", true, false},
        {R"(\[thread-\d+\])", "[thread-12] start", true, true},
        {R"(user=\w+$)", "login user=jörg", true, true},  // \w is Unicode-aware
        {"über", "ÜBER ALLES", false, true},
        {"über", "ÜBER ALLES", true, false},
        {"straße", "STRASSE", false, false},  // PCRE2 folds single code points only
        {"", "anything", true, true},
    }));
    CAPTURE(row.pattern, row.line, row.caseSensitive);
    MatchScratch scratch;
    const auto matcher = compile(row.pattern, {.caseSensitive = row.caseSensitive});
    CHECK(matcher->matches(row.line, scratch) == row.expected);
    CHECK(scratch.skippedLines == 0);
}

TEST_CASE("invalid regex reports message and position", "[search][regex]")
{
    const auto missing = RegexMatcher::compile("abc(def");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().offset == 7);
    CHECK_FALSE(missing.error().message.empty());

    const auto quantifier =
        RegexMatcher::compile("*abc");  // nothing to repeat; PCRE2 points after it
    REQUIRE_FALSE(quantifier.has_value());
    CHECK(quantifier.error().offset == 1);

    const auto bracket = RegexMatcher::compile("[a-");
    REQUIRE_FALSE(bracket.has_value());
    CHECK(bracket.error().offset == 3);
}

TEST_CASE("catastrophic pattern is skipped quickly", "[search][regex]")
{
    const auto matcher = compile("(a+)+$", kSensitive);
    const std::string line = std::string(5000, 'a') + "b";
    MatchScratch scratch;
    const auto start = std::chrono::steady_clock::now();
    CHECK(matcher->match(line, scratch) == RegexOutcome::kSkipped);
    CHECK_FALSE(matcher->matches(line, scratch));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(scratch.skippedLines == 1);
    CHECK(elapsed < std::chrono::milliseconds(200));  // two attempts; 100 ms each
}

TEST_CASE("invalid UTF-8 input is skipped, not matched", "[search][regex]")
{
    const auto matcher = compile("a");
    MatchScratch scratch;
    CHECK(matcher->match("a\xFF", scratch) == RegexOutcome::kSkipped);
}

TEST_CASE("regex uses JIT when available", "[search][regex]")
{
    const auto matcher = compile(R"(\d+ ms)");
    CHECK(matcher->jitCompiled());
}

TEST_CASE("regex collectRanges", "[search][regex]")
{
    MatchScratch scratch;
    std::vector<MatchRange> out;
    compile(R"(\d+)")->collectRanges("a1 b22 c333", scratch, out);
    CHECK(out == std::vector<MatchRange>{{1, 2}, {4, 6}, {8, 11}});

    out.clear();
    compile("x*")->collectRanges("axxbx", scratch, out);  // empty matches are not reported
    CHECK(out == std::vector<MatchRange>{{1, 3}, {4, 5}});

    out.clear();
    compile("ü")->collectRanges("ÜüÜ", scratch, out);  // byte offsets, case-insensitive
    CHECK(out == std::vector<MatchRange>{{0, 2}, {2, 4}, {4, 6}});

    out.clear();
    compile("^")->collectRanges("äb", scratch, out);  // empty match advances by a code point
    CHECK(out.empty());
}

TEST_CASE("one matcher can be shared by threads with their own scratch", "[search][regex]")
{
    const auto matcher = compile(R"(id=(\d+)7\b)");
    std::vector<int> counts(4, 0);
    {
        std::vector<std::jthread> threads;
        threads.reserve(counts.size());
        for (int& count : counts) {
            threads.emplace_back([&matcher, &count] {
                MatchScratch scratch;
                for (int i = 0; i < 2000; ++i) {
                    if (matcher->matches("id=" + std::to_string(i), scratch)) {
                        ++count;
                    }
                }
            });
        }
    }
    for (const int count : counts) {
        CHECK(count == 199);  // numbers 10..1999 ending in 7 (7 itself needs a digit before)
    }
}
