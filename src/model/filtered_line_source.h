#pragma once

#include "model/document_line_source.h"
#include "model/line_source.h"

namespace lv::model
{

class FilteredDocument;

// The matches of a FilteredDocument as a LineSource: row n is the n-th matching line, and
// originalLineNumber() maps back to the file (SPEC F-FILT-1). Text comes from the document's
// line source, so lines shown in both views are decoded and cached once.
//
// Thread-safety: UI thread only.
class FilteredLineSource : public LineSource
{
    Q_OBJECT

public:
    FilteredLineSource(const FilteredDocument& filter, const DocumentLineSource& lines,
                       QObject* parent = nullptr);

    [[nodiscard]] core::LineCount lineCount() const override;
    [[nodiscard]] core::LineNumber originalLineNumber(core::LineNumber row) const override;
    [[nodiscard]] std::vector<DisplayLine> lines(core::LineNumber first,
                                                 core::LineCount count) const override;
    [[nodiscard]] std::vector<DisplayLine> plainLines(core::LineNumber first,
                                                      core::LineCount count) const override;
    [[nodiscard]] std::optional<RowReader> rowReader() const override;

private:
    // Fetches rows through `fetch(firstLine, count)` in runs of consecutive file lines.
    template <typename Fetch>
    [[nodiscard]] std::vector<DisplayLine> collect(core::LineNumber first, core::LineCount count,
                                                   Fetch fetch) const;

    const FilteredDocument* m_filter;
    const DocumentLineSource* m_lines;
};

}  // namespace lv::model
