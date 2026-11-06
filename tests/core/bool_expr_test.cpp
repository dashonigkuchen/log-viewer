#include "core/search/bool_expr.h"
#include "core/search/literal_term.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using lv::core::BoolExpr;
using lv::core::BoolExprOptions;
using lv::core::MatchRange;
using lv::core::MatchScratch;

namespace
{

std::unique_ptr<BoolExpr> parse(std::string_view text, BoolExprOptions options = {})
{
    auto result = BoolExpr::parse(text, options);
    if (!result) {
        FAIL("parse error: " << result.error().message << " at " << result.error().position);
    }
    return std::move(*result);
}

bool matches(std::string_view expr, std::string_view line, BoolExprOptions options = {})
{
    MatchScratch scratch;
    return parse(expr, options)->matches(line, scratch);
}

}  // namespace

TEST_CASE("bool expr precedence and structure", "[search][bool]")
{
    struct Row
    {
        const char* text;
        const char* tree;
    };

    const auto row = GENERATE(values<Row>({
        {R"("a")", R"("a")"},
        {R"("a" AND "b" OR "c")", R"((OR (AND "a" "b") "c"))"},
        {R"("a" OR "b" AND "c")", R"((OR "a" (AND "b" "c")))"},
        {R"(NOT "a" AND "b")", R"((AND (NOT "a") "b"))"},
        {R"(NOT NOT "a")", R"((NOT (NOT "a")))"},
        {R"(("a" OR "b") AND "c")", R"((AND (OR "a" "b") "c"))"},
        {R"("a" and "b" Or not "c")", R"((OR (AND "a" "b") (NOT "c")))"},
        {R"(("a")AND("b"))", R"((AND "a" "b"))"},
        {R"(/ERROR\|FATAL/ AND NOT "heartbeat")", R"((AND /ERROR\|FATAL/ (NOT "heartbeat")))"},
        {R"(/x/i OR "y")", R"((OR /x/i "y"))"},
        {"  \"a\"\t AND\n\"b\"  ", R"((AND "a" "b"))"},
        {R"("a" AND "b" AND "c")", R"((AND "a" "b" "c"))"},
    }));
    CAPTURE(row.text);
    CHECK(parse(row.text)->toString() == row.tree);
}

TEST_CASE("bool expr escapes", "[search][bool]")
{
    CHECK(matches(R"("say \"hi\"")", R"(they say "hi" now)"));
    CHECK_FALSE(matches(R"("say \"hi\"")", "they say hi now"));
    CHECK(matches(R"("C:\\temp")", R"(path C:\temp\x)"));
    CHECK(matches(R"(/a\/b/)", "x a/b y"));         // escaped slash in regex
    CHECK(matches(R"(/\d+ms/)", "took 15ms"));      // other escapes pass through
    CHECK(matches(R"("(not) AND")", "(NOT) and"));  // keywords inside literals are text
}

TEST_CASE("bool expr case sensitivity", "[search][bool]")
{
    CHECK(matches(R"("error")", "ERROR"));
    CHECK_FALSE(matches(R"("error")", "ERROR", {.caseSensitive = true}));
    CHECK(matches(R"(/error/i)", "ERROR", {.caseSensitive = true}));  // per-term flag
    CHECK(matches(R"("größe")", "GRÖSSE x GRÖßE"));                   // non-ASCII folding
    CHECK_FALSE(matches(R"("größe")", "GRÖßE", {.caseSensitive = true}));
}

TEST_CASE("bool expr truth table", "[search][bool]")
{
    struct Row
    {
        const char* line;
        bool andResult;
        bool orResult;
        bool notAnd;
        bool onlyNot;
    };

    // a, b present or absent in the line.
    const auto row = GENERATE(values<Row>({
        {"", false, false, false, true},
        {"a", false, true, true, false},
        {"b", false, true, false, true},
        {"a b", true, true, false, false},
    }));
    CAPTURE(row.line);
    CHECK(matches(R"("a" AND "b")", row.line) == row.andResult);
    CHECK(matches(R"("a" OR "b")", row.line) == row.orResult);
    CHECK(matches(R"("a" AND NOT "b")", row.line) == row.notAnd);
    CHECK(matches(R"(NOT "a")", row.line) == row.onlyNot);
}

TEST_CASE("bool expr errors have positions", "[search][bool]")
{
    struct Row
    {
        const char* text;
        std::size_t position;
        const char* messagePart;
    };

    const auto row = GENERATE(values<Row>({
        {"", 0, "empty"},
        {"   ", 0, "empty"},
        {R"("abc)", 0, "unterminated literal"},
        {R"("a" AND "b\)", 8, "unterminated literal"},
        {R"("a\x")", 2, "invalid escape"},
        {R"(/abc)", 0, "unterminated regex"},
        {R"("a" AND)", 7, "missing term"},
        {R"(AND "a")", 0, "missing term before operator"},
        {R"("a" AND OR "b")", 8, "missing term before operator"},
        {R"(NOT)", 3, "missing term"},
        {R"(("a")", 0, "unmatched '('"},
        {R"("a"))", 3, "unmatched ')'"},
        {R"(())", 1, "empty parentheses"},
        {R"(("a" AND))", 8, "missing term before ')'"},
        {R"("a" "b")", 4, "expected AND or OR"},
        {R"(("a" "b"))", 5, "expected AND, OR, or ')'"},
        {R"(error)", 0, "unknown word"},
        {R"("a" ANDNOT "b")", 4, "unknown word"},
        {R"(/a/x)", 3, "unknown regex flag"},
        {R"(/a/ii)", 4, "unknown regex flag"},
        {R"("a"AND "b")", 3, "expected whitespace"},
        {R"(/a(b/)", 4, "invalid regex"},              // PCRE2 reports the end of the pattern
        {R"("ä" AND "ö)", 8, "unterminated literal"},  // position in code points, not bytes
    }));
    CAPTURE(row.text);
    const auto result = BoolExpr::parse(row.text);
    REQUIRE_FALSE(result.has_value());
    CAPTURE(result.error().message);
    CHECK(result.error().position == row.position);
    CHECK(result.error().message.find(row.messagePart) != std::string::npos);
}

TEST_CASE("bool expr ranges come from positive terms only", "[search][bool]")
{
    MatchScratch scratch;
    std::vector<MatchRange> out;
    parse(R"("ab" AND NOT "cd" OR /b+c/)")->collectRanges("abbc cd", scratch, out);
    // "ab" at 0-2, /b+c/ at 1-4 (merged to 0-4); "cd" is negated and not highlighted.
    CHECK(out == std::vector<MatchRange>{{0, 4}});

    out.clear();
    parse(R"(NOT ("x" OR NOT "y"))")->collectRanges("y x y", scratch, out);  // double negation
    CHECK(out == std::vector<MatchRange>{{0, 1}, {4, 5}});
}

TEST_CASE("escapeRegex keeps the text literal", "[search][bool]")
{
    CHECK(lv::core::escapeRegex("a.b*c") == R"(a\.b\*c)");
    CHECK(lv::core::escapeRegex("größe 1") == "größe 1");
}

namespace
{

// Reference model for the property test: an expression tree evaluated naively.
struct RefNode
{
    enum Kind : std::uint8_t
    {
        kTerm,
        kNot,
        kAnd,
        kOr
    } kind = kTerm;

    std::string term;    // literal text
    bool regex = false;  // term written as /term/ (only letters, so same semantics)
    std::vector<RefNode> children;
};

RefNode randomNode(std::mt19937& rng, int depth)
{
    static constexpr std::array<const char*, 4> kTerms = {"a", "b", "ab", "c"};
    RefNode node;
    const auto pick = rng() % (depth <= 0 ? 1U : 4U);
    if (pick == 0) {
        node.term = kTerms.at(rng() % kTerms.size());
        node.regex = (rng() % 3) == 0;
        return node;
    }
    node.kind = static_cast<RefNode::Kind>(pick);
    const int count = node.kind == RefNode::kNot ? 1 : 2 + static_cast<int>(rng() % 2);
    for (int i = 0; i < count; ++i) {
        node.children.push_back(randomNode(rng, depth - 1));
    }
    return node;
}

std::string render(const RefNode& node)
{
    switch (node.kind) {
    case RefNode::kTerm:
        return node.regex ? "/" + node.term + "/" : "\"" + node.term + "\"";
    case RefNode::kNot:
        return "NOT (" + render(node.children[0]) + ")";
    case RefNode::kAnd:
    case RefNode::kOr: {
        std::string text = "(";
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            if (i > 0) {
                text += node.kind == RefNode::kAnd ? " AND " : " or ";
            }
            text += render(node.children[i]);
        }
        return text + ")";
    }
    }
    return {};
}

bool evaluate(const RefNode& node, const std::string& line)
{
    switch (node.kind) {
    case RefNode::kTerm:
        return line.find(node.term) != std::string::npos;
    case RefNode::kNot:
        return !evaluate(node.children[0], line);
    case RefNode::kAnd:
        return std::ranges::all_of(node.children,
                                   [&](const RefNode& c) { return evaluate(c, line); });
    case RefNode::kOr:
        return std::ranges::any_of(node.children,
                                   [&](const RefNode& c) { return evaluate(c, line); });
    }
    return false;
}

}  // namespace

TEST_CASE("bool expr agrees with a naive reference", "[search][bool]")
{
    std::mt19937 rng(2026);
    MatchScratch scratch;
    for (int i = 0; i < 300; ++i) {
        const RefNode tree = randomNode(rng, 3);
        const std::string text = render(tree);
        CAPTURE(text);
        const auto expr = parse(text, {.caseSensitive = true});
        for (int j = 0; j < 20; ++j) {
            std::string line;
            const std::size_t length = rng() % 6;
            for (std::size_t k = 0; k < length; ++k) {
                line += std::string_view("abcx").at(rng() % 4);
            }
            CAPTURE(line);
            REQUIRE(expr->matches(line, scratch) == evaluate(tree, line));
        }
    }
}
