#pragma once

#include "core/search/matcher.h"
#include "core/util/strong_types.h"
#include "model/highlight_sets.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace lv::model
{

class Document;

// Highlight rules of one tab (SPEC F-HL-3, F-HL-5): applies the tab's active highlight set to
// the lines that are painted, cached per original line number. The active set is either chosen
// for this tab or follows the default set. The cache is cleared when the set or the document
// changes.
//
// Thread-safety: UI thread only.
class LineHighlighter : public QObject
{
    Q_OBJECT

public:
    static constexpr std::size_t kCacheCapacity = 4096;

    explicit LineHighlighter(QObject* parent = nullptr);

    // Clears the cache when `document` starts a new generation.
    void watch(const Document& document);
    // Drops cached results (and repaints).
    void clearCache();

    // Sets to use; null = no highlighting. Not owned.
    void setSets(const HighlightSets* sets);

    [[nodiscard]] const HighlightSets* sets() const noexcept { return m_sets; }

    // Index of the set used by this tab; nullopt = follow the default set.
    void setActiveSet(std::optional<std::size_t> index);

    [[nodiscard]] std::optional<std::size_t> chosenSet() const noexcept { return m_chosen; }

    // The set actually used, if any sets are present.
    [[nodiscard]] std::optional<std::size_t> activeSet() const;

    // Colors for `text`, the displayed text of file line `line`. `partial` lines are not cached.
    [[nodiscard]] LineHighlight highlight(core::LineNumber line, const QString& text,
                                          bool partial) const;

    [[nodiscard]] std::size_t cacheSize() const noexcept { return m_cache.size(); }

signals:
    // The highlighting changed: repaint.
    void changed();

private:
    QPointer<const HighlightSets> m_sets;
    std::optional<std::size_t> m_chosen;
    mutable std::shared_ptr<const CompiledHighlightSet> m_compiled;
    mutable core::MatchScratch m_scratch;
    mutable std::unordered_map<std::uint64_t, LineHighlight> m_cache;
};

}  // namespace lv::model
