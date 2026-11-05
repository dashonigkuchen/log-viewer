#pragma once

#include "core/util/strong_types.h"
#include "model/text_copy.h"

#include <QObject>
#include <QString>

#include <optional>
#include <vector>

namespace lv::model
{

// One line prepared for display (ARCHITECTURE §5).
struct DisplayLine
{
    // Decoded text without the line terminator; control characters are replaced by visible
    // symbols, tabs are kept (the view expands them).
    QString text;
    // The raw line was longer than the reader's limit and was cut (F-FILE-7).
    bool truncated = false;
    // The last line of the file without a line feed yet; it may still change (F-TAIL-8).
    bool partial = false;
    // Reading failed (for example, the file shrank); text is empty.
    bool unavailable = false;
};

// What a view shows: the full file or the result of a filter. Rows are positions in the view,
// original line numbers are line numbers in the file. The view does not know which one it shows.
//
// Thread-safety: UI thread only.
class LineSource : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    [[nodiscard]] virtual core::LineCount lineCount() const = 0;

    // Line number in the file of `row` (< lineCount()).
    [[nodiscard]] virtual core::LineNumber originalLineNumber(core::LineNumber row) const = 0;

    // Rows [first, first + count) with first + count <= lineCount().
    [[nodiscard]] virtual std::vector<DisplayLine> lines(core::LineNumber first,
                                                         core::LineCount count) const = 0;

    // Same rows with the original text for copying: control characters are not replaced by
    // visible symbols (the UTF-16 columns are the same as in lines()). Not cached.
    [[nodiscard]] virtual std::vector<DisplayLine> plainLines(core::LineNumber first,
                                                              core::LineCount count) const = 0;

    // Thread-safe access to the rows for large copies on a worker (SPEC F-VIEW-8), or nullopt
    // if the source has no file behind it (copies then run synchronously).
    [[nodiscard]] virtual std::optional<RowReader> rowReader() const { return std::nullopt; }

signals:
    // Rows were added at the end; `lineCount` is the new count.
    void linesAppended(quint64 lineCount);
    // All rows changed (reload, encoding change, new filter); line numbers may be invalid.
    void reset();
};

}  // namespace lv::model
