#pragma once

#include "core/search/matcher.h"

#include <tl/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lv::core
{

// Parse error of a boolean expression. `position` counts code points (not bytes) from the start
// of the expression, so the UI can point at the character.
struct ExprError
{
    std::string message;
    std::size_t position = 0;

    friend bool operator==(const ExprError&, const ExprError&) = default;
};

struct BoolExprOptions
{
    // Case sensitivity of all terms; a regex term with flag `i` is always case-insensitive.
    bool caseSensitive = false;
};

// Boolean filter expression (SPEC F-FILT-4, grammar in SPEC §5.1): quoted literals and /regex/
// terms combined with AND, OR, NOT, and parentheses; precedence NOT > AND > OR.
// Evaluation short-circuits, and literal terms are tried before regex terms inside AND/OR.
// Thread-safety: immutable, share freely.
class BoolExpr final : public Matcher
{
public:
    [[nodiscard]] static tl::expected<std::unique_ptr<BoolExpr>, ExprError>
    parse(std::string_view text, BoolExprOptions options = {});

    ~BoolExpr() override;
    BoolExpr(const BoolExpr&) = delete;
    BoolExpr& operator=(const BoolExpr&) = delete;
    BoolExpr(BoolExpr&&) = delete;
    BoolExpr& operator=(BoolExpr&&) = delete;

    // Canonical prefix form of the parsed tree, for tests and debugging, for example
    // `(OR (AND "a" (NOT /b/i)) "c")`. AND/OR children appear in source order.
    [[nodiscard]] std::string toString() const;

    [[nodiscard]] bool matches(std::string_view line, MatchScratch& scratch) const override;

    // Ranges of positive (not negated) terms, sorted and merged so they do not overlap.
    void collectRanges(std::string_view line, MatchScratch& scratch,
                       std::vector<MatchRange>& out) const override;

    enum class Kind : std::uint8_t
    {
        kTerm,
        kNot,
        kAnd,
        kOr,
    };

    struct Node
    {
        Kind kind = Kind::kTerm;
        std::vector<std::size_t> children;  // indices into m_nodes
        // Evaluation order of children (cheap terms first); same elements as `children`.
        std::vector<std::size_t> order;
        std::unique_ptr<Matcher> matcher;  // kTerm only
        std::string source;                // kTerm only: the term as written, for toString()
        bool regex = false;                // kTerm only
    };

private:
    BoolExpr() = default;

    [[nodiscard]] bool evaluate(std::size_t node, std::string_view line,
                                MatchScratch& scratch) const;
    void collect(std::size_t node, bool negated, std::string_view line, MatchScratch& scratch,
                 std::vector<MatchRange>& out) const;
    [[nodiscard]] std::string toString(std::size_t node) const;
    [[nodiscard]] bool isRegexHeavy(std::size_t node) const;

    friend class BoolExprParser;

    std::vector<Node> m_nodes;
    std::size_t m_root = 0;
};

}  // namespace lv::core
