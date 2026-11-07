#pragma once

#include "core/search/matcher.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lv::core
{

struct LiteralOptions
{
    bool caseSensitive = false;
    // The match must not be preceded or followed by a word character: ASCII letters, digits,
    // underscore, or any non-ASCII byte (so UTF-8 letters such as "ü" count as word characters).
    bool wholeWord = false;
};

// Plain substring matcher (SPEC F-FILT-2). Case-insensitive matching folds ASCII letters only;
// query compilation (M3-04) routes case-insensitive patterns with non-ASCII letters to
// RegexMatcher instead. An empty pattern matches every line and yields no ranges.
// Thread-safety: immutable, share freely.
class LiteralMatcher final : public Matcher
{
public:
    LiteralMatcher(std::string pattern, LiteralOptions options);

    [[nodiscard]] const std::string& pattern() const noexcept { return m_pattern; }

    [[nodiscard]] const LiteralOptions& options() const noexcept { return m_options; }

    // True if `pattern` contains only ASCII bytes (its case folding is fully handled here).
    [[nodiscard]] static bool isAscii(std::string_view pattern) noexcept;

    [[nodiscard]] bool matches(std::string_view line, MatchScratch& scratch) const override;
    // Candidates are raw occurrences of the pattern (whole-word is checked per line).
    [[nodiscard]] bool hasCandidateScan() const noexcept override;
    [[nodiscard]] std::size_t nextCandidate(std::string_view text, std::size_t from) const override;
    void collectRanges(std::string_view line, MatchScratch& scratch,
                       std::vector<MatchRange>& out) const override;

private:
    // Position of the first match starting at or after `from`.
    [[nodiscard]] std::optional<std::size_t> find(std::string_view line, std::size_t from) const;
    [[nodiscard]] std::optional<std::size_t> findRaw(std::string_view line, std::size_t from) const;

    std::string m_pattern;  // lowercased (ASCII) when case-insensitive
    LiteralOptions m_options;
};

}  // namespace lv::core
