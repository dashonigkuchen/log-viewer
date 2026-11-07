#include "core/search/literal_matcher.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>

#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define LV_LITERAL_SSE2 1  // NOLINT(cppcoreguidelines-macro-usage)
#else
#define LV_LITERAL_SSE2 0  // NOLINT(cppcoreguidelines-macro-usage)
#endif

namespace lv::core
{

namespace
{

constexpr char toLowerAscii(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr char toUpperAscii(char c) noexcept
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

constexpr bool isWordByte(char c) noexcept
{
    const auto byte = static_cast<unsigned char>(c);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '_' || byte >= 0x80;
}

// Folded comparison of `text` (any case) with `lowerPattern` (already lowercased).
bool equalsFolded(std::string_view text, std::string_view lowerPattern) noexcept
{
    for (std::size_t i = 0; i < lowerPattern.size(); ++i) {
        if (toLowerAscii(text[i]) != lowerPattern[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

LiteralMatcher::LiteralMatcher(std::string pattern, LiteralOptions options)
    : m_pattern(std::move(pattern))
    , m_options(options)
{
    if (!m_options.caseSensitive) {
        std::ranges::transform(m_pattern, m_pattern.begin(), toLowerAscii);
    }
}

bool LiteralMatcher::isAscii(std::string_view pattern) noexcept
{
    return std::ranges::all_of(pattern,
                               [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

#if LV_LITERAL_SSE2
namespace
{

// Candidate starts where the first two pattern bytes match (both cases for letters when
// case-insensitive), 16 positions at a time. Returns the first verified match or npos.
std::size_t findPairSse2(std::string_view text, std::size_t from, std::string_view pattern,
                         bool caseSensitive)
{
    const std::size_t length = pattern.size();
    const std::size_t last = text.size() - length;  // last possible start
    const char a = pattern[0];
    const char b = pattern[1];
    const char aUpper = caseSensitive ? a : toUpperAscii(a);
    const char bUpper = caseSensitive ? b : toUpperAscii(b);
    const __m128i a1 = _mm_set1_epi8(a);
    const __m128i a2 = _mm_set1_epi8(aUpper);
    const __m128i b1 = _mm_set1_epi8(b);
    const __m128i b2 = _mm_set1_epi8(bUpper);
    const auto verify = [&](std::size_t start) {
        const std::string_view candidate = text.substr(start, length);
        return caseSensitive ? candidate == pattern : equalsFolded(candidate, pattern);
    };
    std::size_t pos = from;
    // Loads read [pos, pos + 17): keep them inside the text.
    while (pos + 17 <= text.size() && pos <= last) {
        const char* p = text.data() + pos;
        const __m128i first = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));       // NOLINT
        const __m128i second = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 1));  // NOLINT
        const __m128i hitA = _mm_or_si128(_mm_cmpeq_epi8(first, a1), _mm_cmpeq_epi8(first, a2));
        const __m128i hitB = _mm_or_si128(_mm_cmpeq_epi8(second, b1), _mm_cmpeq_epi8(second, b2));
        auto mask = static_cast<unsigned>(_mm_movemask_epi8(_mm_and_si128(hitA, hitB)));
        while (mask != 0) {
            const std::size_t start = pos + static_cast<std::size_t>(std::countr_zero(mask));
            if (start > last) {
                return std::string_view::npos;
            }
            if (verify(start)) {
                return start;
            }
            mask &= mask - 1;
        }
        pos += 16;
    }
    for (; pos <= last; ++pos) {
        if (verify(pos)) {
            return pos;
        }
    }
    return std::string_view::npos;
}

}  // namespace
#endif

std::optional<std::size_t> LiteralMatcher::findRaw(std::string_view line, std::size_t from) const
{
    const std::size_t length = m_pattern.size();
    if (from > line.size() || line.size() - from < length) {
        return std::nullopt;
    }
#if LV_LITERAL_SSE2
    if (length >= 2) {
        const std::size_t pos = findPairSse2(line, from, m_pattern, m_options.caseSensitive);
        return pos == std::string_view::npos ? std::nullopt : std::optional(pos);
    }
#endif
    if (m_options.caseSensitive) {
        const std::size_t pos = line.find(m_pattern, from);
        return pos == std::string_view::npos ? std::nullopt : std::optional(pos);
    }
    // Case-insensitive ASCII: look for either case of the first byte with memchr, then compare.
    const char lower = m_pattern.front();
    const char upper = toUpperAscii(lower);
    const std::string_view rest = std::string_view(m_pattern).substr(1);
    const std::size_t last = line.size() - length;  // last possible start
    std::size_t pos = from;
    while (pos <= last) {
        const char* base = line.data() + pos;
        const std::size_t span = last - pos + 1;
        const auto* hitLower = static_cast<const char*>(std::memchr(base, lower, span));
        const char* hit = hitLower;
        if (upper != lower) {
            const std::size_t limit =
                hitLower != nullptr ? static_cast<std::size_t>(hitLower - base) : span;
            if (const auto* hitUpper = static_cast<const char*>(std::memchr(base, upper, limit))) {
                hit = hitUpper;
            }
        }
        if (hit == nullptr) {
            return std::nullopt;
        }
        const auto candidate = static_cast<std::size_t>(hit - line.data());
        if (equalsFolded(line.substr(candidate + 1, rest.size()), rest)) {
            return candidate;
        }
        pos = candidate + 1;
    }
    return std::nullopt;
}

std::optional<std::size_t> LiteralMatcher::find(std::string_view line, std::size_t from) const
{
    std::size_t pos = from;
    while (true) {
        const auto hit = findRaw(line, pos);
        if (!hit || !m_options.wholeWord) {
            return hit;
        }
        const std::size_t end = *hit + m_pattern.size();
        const bool startOk = *hit == 0 || !isWordByte(line[*hit - 1]);
        const bool endOk = end == line.size() || !isWordByte(line[end]);
        if (startOk && endOk) {
            return hit;
        }
        pos = *hit + 1;
    }
}

bool LiteralMatcher::hasCandidateScan() const noexcept
{
    // A pattern with a line terminator could match across lines in a chunk.
    return !m_pattern.empty() && m_pattern.find_first_of("\r\n") == std::string::npos;
}

std::size_t LiteralMatcher::nextCandidate(std::string_view text, std::size_t from) const
{
    return findRaw(text, from).value_or(std::string_view::npos);
}

bool LiteralMatcher::matches(std::string_view line, MatchScratch& /*scratch*/) const
{
    return m_pattern.empty() || find(line, 0).has_value();
}

void LiteralMatcher::collectRanges(std::string_view line, MatchScratch& /*scratch*/,
                                   std::vector<MatchRange>& out) const
{
    if (m_pattern.empty()) {
        return;
    }
    std::size_t pos = 0;
    while (const auto hit = find(line, pos)) {
        out.push_back({*hit, *hit + m_pattern.size()});
        pos = *hit + m_pattern.size();
    }
}

}  // namespace lv::core
