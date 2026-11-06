#include "core/search/literal_term.h"

#include "core/search/literal_matcher.h"
#include "core/search/regex_matcher.h"
#include "core/util/assert.h"

namespace lv::core
{

std::string escapeRegex(std::string_view text)
{
    std::string out;
    out.reserve(text.size() * 2);
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        const bool alnum = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                           (byte >= '0' && byte <= '9');
        if (byte < 0x80 && !alnum && byte != ' ') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

std::unique_ptr<Matcher> makeLiteralMatcher(std::string_view text, bool caseSensitive,
                                            bool wholeWord)
{
    if (caseSensitive || LiteralMatcher::isAscii(text) || text.empty()) {
        return std::make_unique<LiteralMatcher>(
            std::string(text),
            LiteralOptions{.caseSensitive = caseSensitive, .wholeWord = wholeWord});
    }
    // Word boundaries: no word character (Unicode \w with UCP) directly before or after.
    std::string pattern = escapeRegex(text);
    if (wholeWord) {
        pattern = "(?<!\\w)" + pattern + "(?!\\w)";
    }
    auto regex = RegexMatcher::compile(pattern, RegexOptions{.caseSensitive = false});
    LV_ASSERT(regex.has_value());  // an escaped literal always compiles
    return std::move(*regex);
}

}  // namespace lv::core
