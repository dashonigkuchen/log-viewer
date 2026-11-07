#pragma once

#include "core/search/matcher.h"

#include <tl/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace lv::core
{

enum class QueryMode : std::uint8_t
{
    kLiteral,
    kRegex,
    kBoolean,
};

[[nodiscard]] std::string_view toString(QueryMode mode) noexcept;

// What the user entered in a filter or find field (SPEC F-FILT-2): the text, the mode, and the
// options. Equality and hashing cover all fields, for the filter history and result caching.
struct Query
{
    std::string text;
    QueryMode mode = QueryMode::kLiteral;
    bool caseSensitive = false;
    // Literal mode only; ignored (and not compared) in the other modes.
    bool wholeWord = false;
    // Show lines that do NOT match.
    bool invert = false;

    // Copy with option fields that have no effect in `mode` reset, so equal queries compare
    // and hash equal.
    [[nodiscard]] Query normalized() const;

    friend bool operator==(const Query& a, const Query& b);
};

// Error from compiling a query: message and position in code points (for the input field).
struct QueryError
{
    std::string message;
    std::size_t position = 0;

    friend bool operator==(const QueryError&, const QueryError&) = default;
};

// A compiled query: a matcher (inverted if requested) and the query it came from. Immutable and
// shared between search threads.
class CompiledQuery final : public Matcher
{
public:
    [[nodiscard]] static tl::expected<std::shared_ptr<const CompiledQuery>, QueryError>
    compile(const Query& query);

    CompiledQuery(Query query, std::unique_ptr<Matcher> matcher);
    ~CompiledQuery() override;
    CompiledQuery(const CompiledQuery&) = delete;
    CompiledQuery& operator=(const CompiledQuery&) = delete;
    CompiledQuery(CompiledQuery&&) = delete;
    CompiledQuery& operator=(CompiledQuery&&) = delete;

    [[nodiscard]] const Query& query() const noexcept { return m_query; }

    [[nodiscard]] bool matches(std::string_view line, MatchScratch& scratch) const override;
    // Forwarded from the underlying matcher; inverted queries cannot skip lines.
    [[nodiscard]] bool hasCandidateScan() const noexcept override;
    [[nodiscard]] std::size_t nextCandidate(std::string_view text, std::size_t from) const override;

    // Highlight ranges of the underlying matcher; empty for inverted queries (a non-matching
    // line has nothing to highlight).
    void collectRanges(std::string_view line, MatchScratch& scratch,
                       std::vector<MatchRange>& out) const override;

private:
    Query m_query;
    std::unique_ptr<Matcher> m_matcher;
};

}  // namespace lv::core

template <>
struct std::hash<lv::core::Query>
{
    std::size_t operator()(const lv::core::Query& query) const noexcept;
};
