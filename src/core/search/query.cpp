#include "core/search/query.h"

#include "core/search/bool_expr.h"
#include "core/search/literal_term.h"
#include "core/search/regex_matcher.h"

#include <functional>
#include <utility>

namespace lv::core
{

namespace
{

std::size_t codePoints(std::string_view text, std::size_t bytes) noexcept
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < bytes && i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

}  // namespace

std::string_view toString(QueryMode mode) noexcept
{
    switch (mode) {
    case QueryMode::kLiteral:
        return "Literal";
    case QueryMode::kRegex:
        return "Regex";
    case QueryMode::kBoolean:
        return "Boolean";
    }
    return "Unknown";
}

Query Query::normalized() const
{
    Query copy = *this;
    if (copy.mode != QueryMode::kLiteral) {
        copy.wholeWord = false;
    }
    return copy;
}

bool operator==(const Query& a, const Query& b)
{
    const Query x = a.normalized();
    const Query y = b.normalized();
    return x.text == y.text && x.mode == y.mode && x.caseSensitive == y.caseSensitive &&
           x.wholeWord == y.wholeWord && x.invert == y.invert;
}

tl::expected<std::shared_ptr<const CompiledQuery>, QueryError>
CompiledQuery::compile(const Query& query)
{
    const Query normalized = query.normalized();
    std::unique_ptr<Matcher> matcher;
    switch (normalized.mode) {
    case QueryMode::kLiteral:
        matcher =
            makeLiteralMatcher(normalized.text, normalized.caseSensitive, normalized.wholeWord);
        break;
    case QueryMode::kRegex: {
        auto regex = RegexMatcher::compile(normalized.text,
                                           RegexOptions{.caseSensitive = normalized.caseSensitive});
        if (!regex) {
            return tl::unexpected(
                QueryError{.message = regex.error().message,
                           .position = codePoints(normalized.text, regex.error().offset)});
        }
        matcher = std::move(*regex);
        break;
    }
    case QueryMode::kBoolean: {
        auto expr = BoolExpr::parse(normalized.text,
                                    BoolExprOptions{.caseSensitive = normalized.caseSensitive});
        if (!expr) {
            return tl::unexpected(
                QueryError{.message = expr.error().message, .position = expr.error().position});
        }
        matcher = std::move(*expr);
        break;
    }
    }
    return std::make_shared<const CompiledQuery>(normalized, std::move(matcher));
}

CompiledQuery::CompiledQuery(Query query, std::unique_ptr<Matcher> matcher)
    : m_query(std::move(query))
    , m_matcher(std::move(matcher))
{}

CompiledQuery::~CompiledQuery() = default;

bool CompiledQuery::hasCandidateScan() const noexcept
{
    return !m_query.invert && m_matcher->hasCandidateScan();
}

std::size_t CompiledQuery::nextCandidate(std::string_view text, std::size_t from) const
{
    return m_matcher->nextCandidate(text, from);
}

bool CompiledQuery::matches(std::string_view line, MatchScratch& scratch) const
{
    return m_matcher->matches(line, scratch) != m_query.invert;
}

void CompiledQuery::collectRanges(std::string_view line, MatchScratch& scratch,
                                  std::vector<MatchRange>& out) const
{
    if (!m_query.invert) {
        m_matcher->collectRanges(line, scratch, out);
    }
}

}  // namespace lv::core

std::size_t std::hash<lv::core::Query>::operator()(const lv::core::Query& query) const noexcept
{
    const lv::core::Query q = query.normalized();
    std::size_t seed = std::hash<std::string>{}(q.text);
    const auto flags = static_cast<std::size_t>(q.mode) | (q.caseSensitive ? 0x10U : 0U) |
                       (q.wholeWord ? 0x20U : 0U) | (q.invert ? 0x40U : 0U);
    // Boost-style combine.
    seed ^= flags + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}
