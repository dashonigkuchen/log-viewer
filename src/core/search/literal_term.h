#pragma once

#include "core/search/matcher.h"

#include <memory>
#include <string>
#include <string_view>

namespace lv::core
{

// Escapes `text` so PCRE2 matches it literally (ASCII punctuation is backslash-escaped,
// letters, digits, and non-ASCII bytes are kept).
[[nodiscard]] std::string escapeRegex(std::string_view text);

// Matcher for a plain-text term. Uses LiteralMatcher, except for case-insensitive patterns with
// non-ASCII letters, which need Unicode case folding and therefore become an escaped
// RegexMatcher (whole word then uses Unicode word boundaries).
[[nodiscard]] std::unique_ptr<Matcher> makeLiteralMatcher(std::string_view text, bool caseSensitive,
                                                          bool wholeWord);

}  // namespace lv::core
