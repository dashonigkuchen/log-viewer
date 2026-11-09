#pragma once

#include "model/document_line_source.h"
#include "model/line_source.h"

#include <QPointer>

#include <cstdint>
#include <vector>

namespace lv::model
{

class FilteredDocument;
class MarkSet;

// The matches of a FilteredDocument as a LineSource: row n is the n-th matching line, and
// originalLineNumber() maps back to the file (SPEC F-FILT-1). Text comes from the document's
// line source, so lines shown in both views are decoded and cached once.
//
// With marks shown (F-FILT-9, default on), marked lines that do not match are merged into the
// rows in line order and flagged as DisplayLine::extra. Changing marks (or new matches while
// such rows exist) inserts or removes rows in the middle: the source emits rowsAboutToChange()
// and rowsChanged(), and views keep their position by original line number.
//
// Thread-safety: UI thread only.
class FilteredLineSource : public LineSource
{
    Q_OBJECT

public:
    FilteredLineSource(const FilteredDocument& filter, const DocumentLineSource& lines,
                       QObject* parent = nullptr);

    // Marks to merge; null = none. Not owned.
    void setMarks(const MarkSet* marks);
    void setShowMarks(bool show);

    [[nodiscard]] bool showMarks() const noexcept { return m_showMarks; }

    [[nodiscard]] core::LineCount lineCount() const override;
    [[nodiscard]] core::LineNumber originalLineNumber(core::LineNumber row) const override;
    [[nodiscard]] std::vector<DisplayLine> lines(core::LineNumber first,
                                                 core::LineCount count) const override;
    [[nodiscard]] std::vector<DisplayLine> plainLines(core::LineNumber first,
                                                      core::LineCount count) const override;
    [[nodiscard]] std::optional<RowReader> rowReader() const override;

    // Number of rows whose original line is < `line`.
    [[nodiscard]] std::uint64_t rowsBefore(core::LineNumber line) const;
    // Whether `row` is a marked line that does not match (F-FILT-9).
    [[nodiscard]] bool isExtraRow(core::LineNumber row) const;

private:
    // Announced matches plus the marked lines that are not matches; extraRows[k] is the row of
    // extraLines[k] (both ascending).
    struct Layout
    {
        std::uint64_t matchCount = 0;
        std::vector<std::uint64_t> extraLines;
        std::vector<std::uint64_t> extraRows;
    };

    void rebuild(std::uint64_t matchCount);
    void onMatchesAppended(quint64 matchCount);
    void onFilterReset();
    void onMarksChanged();

    // Fetches rows through `fetch(firstLine, count)` in runs of consecutive file lines.
    template <typename Fetch>
    [[nodiscard]] std::vector<DisplayLine> collect(core::LineNumber first, core::LineCount count,
                                                   Fetch fetch) const;

    const FilteredDocument* m_filter;
    const DocumentLineSource* m_lines;
    QPointer<const MarkSet> m_marks;
    bool m_showMarks = true;
    Layout m_layout;
};

}  // namespace lv::model
