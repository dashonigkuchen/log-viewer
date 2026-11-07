#include "model/match_highlighter.h"

#include "core/search/query.h"
#include "model/document.h"
#include "model/filtered_document.h"

#include <string>
#include <string_view>

namespace lv::model
{

std::vector<HighlightRange> highlightRanges(const core::Matcher& matcher, const QString& text,
                                            core::MatchScratch& scratch)
{
    // UTF-8 copy of the text and the UTF-16 column at which every UTF-8 byte's character starts.
    std::string utf8;
    std::vector<qsizetype> columnOfByte;
    utf8.reserve(static_cast<std::size_t>(text.size()));
    columnOfByte.reserve(static_cast<std::size_t>(text.size()));
    const auto put = [&](char32_t c, qsizetype column) {
        const auto push = [&](unsigned value) {
            utf8 += static_cast<char>(value);
            columnOfByte.push_back(column);
        };
        if (c < 0x80) {
            push(c);
        } else if (c < 0x800) {
            push(0xC0U | (c >> 6U));
            push(0x80U | (c & 0x3FU));
        } else if (c < 0x10000) {
            push(0xE0U | (c >> 12U));
            push(0x80U | ((c >> 6U) & 0x3FU));
            push(0x80U | (c & 0x3FU));
        } else {
            push(0xF0U | (c >> 18U));
            push(0x80U | ((c >> 12U) & 0x3FU));
            push(0x80U | ((c >> 6U) & 0x3FU));
            push(0x80U | (c & 0x3FU));
        }
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t unit = text.at(i).unicode();
        if (QChar::isHighSurrogate(unit) && i + 1 < text.size() &&
            QChar::isLowSurrogate(text.at(i + 1).unicode())) {
            put(QChar::surrogateToUcs4(unit, text.at(i + 1).unicode()), i);
            ++i;
        } else if (QChar::isSurrogate(unit)) {
            put(0xFFFD, i);  // lone surrogate: keep the UTF-8 valid
        } else {
            put(unit, i);
        }
    }
    columnOfByte.push_back(text.size());  // end

    std::vector<core::MatchRange> matches;
    matcher.collectRanges(utf8, scratch, matches);
    std::vector<HighlightRange> out;
    out.reserve(matches.size());
    for (const auto& match : matches) {
        const qsizetype from = columnOfByte[std::min(match.begin, utf8.size())];
        // A match ending inside a character still covers that character.
        std::size_t end = std::min(match.end, utf8.size());
        while (end < utf8.size() && (static_cast<unsigned char>(utf8[end]) & 0xC0U) == 0x80U) {
            ++end;
        }
        const qsizetype to = columnOfByte[end];
        if (to > from) {
            out.push_back({from, to});
        }
    }
    return out;
}

MatchHighlighter::MatchHighlighter(const FilteredDocument& filter, QObject* parent)
    : QObject(parent)
    , m_filter(&filter)
{
    connect(&filter, &FilteredDocument::reset, this, &MatchHighlighter::invalidate);
    connect(&filter.document(), &Document::reset, this, &MatchHighlighter::invalidate);
}

void MatchHighlighter::invalidate()
{
    m_cache.clear();
    m_scratch = core::MatchScratch{};
    emit changed();
}

std::vector<HighlightRange> MatchHighlighter::ranges(core::LineNumber line, const QString& text,
                                                     bool partial) const
{
    const auto query = m_filter->compiledQuery();
    if (!query || query->query().invert) {
        return {};
    }
    if (!partial) {
        if (const auto it = m_cache.find(line.get()); it != m_cache.end()) {
            return it->second;
        }
    }
    auto result = highlightRanges(*query, text, m_scratch);
    if (!partial) {
        if (m_cache.size() >= kCacheCapacity) {
            m_cache.clear();  // simple bound; painted lines are recomputed cheaply
        }
        m_cache.emplace(line.get(), result);
    }
    return result;
}

}  // namespace lv::model
