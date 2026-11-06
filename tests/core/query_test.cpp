#include "core/search/query.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <unordered_set>
#include <vector>

using lv::core::CompiledQuery;
using lv::core::MatchRange;
using lv::core::MatchScratch;
using lv::core::Query;
using lv::core::QueryMode;

namespace
{

bool matches(const Query& query, std::string_view line)
{
    auto compiled = CompiledQuery::compile(query);
    REQUIRE(compiled.has_value());
    MatchScratch scratch;
    return (*compiled)->matches(line, scratch);
}

}  // namespace

TEST_CASE("query modes and options", "[search][query]")
{
    struct Row
    {
        Query query;
        const char* line;
        bool expected;
    };

    const auto row = GENERATE(values<Row>({
        // Literal
        {{.text = "error"}, "ERROR x", true},
        {{.text = "error", .caseSensitive = true}, "ERROR x", false},
        {{.text = "err", .wholeWord = true}, "error", false},
        {{.text = "error", .wholeWord = true}, "an error!", true},
        {{.text = "größe"}, "GRÖßE", true},  // non-ASCII case folding via regex
        {{.text = "größe", .wholeWord = true}, "xgrößex", false},
        {{.text = "größe", .wholeWord = true}, "die GRÖßE.", true},
        {{.text = "a.c"}, "abc", false},  // literal, not a regex
        {{.text = "error", .invert = true}, "ERROR x", false},
        {{.text = "error", .invert = true}, "fine", true},
        // Regex
        {{.text = "a.c", .mode = QueryMode::kRegex}, "abc", true},
        {{.text = "^ERR", .mode = QueryMode::kRegex}, "err x", true},
        {{.text = "^ERR", .mode = QueryMode::kRegex, .caseSensitive = true}, "err x", false},
        {{.text = "\\d+", .mode = QueryMode::kRegex, .invert = true}, "no digits", true},
        // whole word has no effect outside literal mode
        {{.text = "err", .mode = QueryMode::kRegex, .wholeWord = true}, "error", true},
        // Boolean
        {{.text = R"("a" AND NOT "b")", .mode = QueryMode::kBoolean}, "A c", true},
        {{.text = R"("a" AND NOT "b")", .mode = QueryMode::kBoolean, .caseSensitive = true},
         "A c",
         false},
        {{.text = R"("a" AND NOT "b")", .mode = QueryMode::kBoolean, .invert = true}, "a b", true},
    }));
    CAPTURE(row.query.text, toString(row.query.mode), row.query.caseSensitive, row.query.wholeWord,
            row.query.invert, row.line);
    CHECK(matches(row.query, row.line) == row.expected);
}

TEST_CASE("empty literal query matches everything", "[search][query]")
{
    CHECK(matches({.text = ""}, "anything"));
    CHECK_FALSE(matches({.text = "", .invert = true}, "anything"));
}

TEST_CASE("query compile errors", "[search][query]")
{
    const auto regex = CompiledQuery::compile({.text = "ä(b", .mode = QueryMode::kRegex});
    REQUIRE_FALSE(regex.has_value());
    CHECK(regex.error().position == 3);  // code points, PCRE2 reports the end
    const auto boolean =
        CompiledQuery::compile({.text = R"("a" AND)", .mode = QueryMode::kBoolean});
    REQUIRE_FALSE(boolean.has_value());
    CHECK(boolean.error().position == 7);
    CHECK(CompiledQuery::compile({.text = "(", .mode = QueryMode::kLiteral}).has_value());
}

TEST_CASE("inverted queries have no highlight ranges", "[search][query]")
{
    MatchScratch scratch;
    std::vector<MatchRange> out;
    const auto plain = *CompiledQuery::compile({.text = "b"});
    plain->collectRanges("abcb", scratch, out);
    CHECK(out == std::vector<MatchRange>{{1, 2}, {3, 4}});
    out.clear();
    const auto inverted = *CompiledQuery::compile({.text = "b", .invert = true});
    inverted->collectRanges("abcb", scratch, out);
    CHECK(out.empty());
}

TEST_CASE("query equality and hashing", "[search][query]")
{
    const Query a{.text = "x", .mode = QueryMode::kRegex, .wholeWord = true};
    const Query b{.text = "x", .mode = QueryMode::kRegex, .wholeWord = false};
    CHECK(a == b);  // whole word is irrelevant in regex mode
    CHECK(std::hash<Query>{}(a) == std::hash<Query>{}(b));
    const Query c{.text = "x", .wholeWord = true};
    const Query d{.text = "x", .wholeWord = false};
    CHECK_FALSE(c == d);

    std::unordered_set<Query> set;
    for (const auto mode : {QueryMode::kLiteral, QueryMode::kRegex, QueryMode::kBoolean}) {
        for (int bits = 0; bits < 8; ++bits) {
            set.insert(Query{.text = "t",
                             .mode = mode,
                             .caseSensitive = (bits & 1) != 0,
                             .wholeWord = (bits & 2) != 0,
                             .invert = (bits & 4) != 0});
        }
    }
    CHECK(set.size() == 8 + 4 + 4);  // whole word only distinguishes literal queries
    CHECK(set.contains(Query{.text = "t", .mode = QueryMode::kBoolean, .wholeWord = true}));
    CHECK_FALSE(set.contains(Query{.text = "u"}));
}

TEST_CASE("compiled query keeps the normalized query", "[search][query]")
{
    const auto compiled =
        CompiledQuery::compile({.text = R"("x")", .mode = QueryMode::kBoolean, .wholeWord = true});
    REQUIRE(compiled.has_value());
    CHECK_FALSE((*compiled)->query().wholeWord);
    CHECK(toString(QueryMode::kBoolean) == "Boolean");
}
