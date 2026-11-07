#pragma once

#include "core/search/matcher.h"
#include "core/util/strong_types.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lv::model
{

class FilteredDocument;

// Half-open range [from, to) of UTF-16 columns in a displayed line.
struct HighlightRange
{
    qsizetype from = 0;
    qsizetype to = 0;

    friend bool operator==(const HighlightRange&, const HighlightRange&) = default;
};

// Finds the parts of a line that match a UTF-8 matcher and returns them in UTF-16 columns.
// Works for every file encoding because it matches the decoded text (re-encoded as UTF-8).
[[nodiscard]] std::vector<HighlightRange>
highlightRanges(const core::Matcher& matcher, const QString& text, core::MatchScratch& scratch);

// Match highlighting of the current filter for both views of a tab (SPEC F-FILT-8). Computed
// only for lines that are painted, cached per original line number, and cleared when the
// filter or the document changes. Only positive terms are highlighted; inverted filters
// highlight nothing.
//
// Thread-safety: UI thread only.
class MatchHighlighter : public QObject
{
    Q_OBJECT

public:
    static constexpr std::size_t kCacheCapacity = 4096;

    explicit MatchHighlighter(const FilteredDocument& filter, QObject* parent = nullptr);

    // Ranges for `text`, the displayed text of file line `line`. `partial` lines (still being
    // written) are not cached.
    [[nodiscard]] std::vector<HighlightRange> ranges(core::LineNumber line, const QString& text,
                                                     bool partial) const;

    [[nodiscard]] std::size_t cacheSize() const noexcept { return m_cache.size(); }

signals:
    // Highlights changed (new filter, cleared filter, document reset): repaint.
    void changed();

private:
    void invalidate();

    const FilteredDocument* m_filter;
    mutable core::MatchScratch m_scratch;
    mutable std::unordered_map<std::uint64_t, std::vector<HighlightRange>> m_cache;
};

}  // namespace lv::model
