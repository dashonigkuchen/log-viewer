#pragma once

#include "core/util/strong_types.h"
#include "model/line_source.h"
#include "ui/text_selection.h"

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace lv::ui
{

// Shows the rows of a LineSource (ARCHITECTURE §6). Only the visible rows are read and painted.
// Line numbers are 64-bit: QScrollBar is int-based, so for more than kMaxScrollRange rows the
// scroll bar range is scaled while the exact top row is kept separately (SPEC F-VIEW-2/4/5/10,
// F-FILE-7).
//
// Thread-safety: UI thread only.
class LogView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    static constexpr int kMaxScrollRange = 1 << 30;
    static constexpr int kDefaultTabWidth = 8;
    static constexpr int kDefaultColumnLimit = 100000;
    // Appended to text cut at the column limit or by the reader (F-FILE-7).
    static constexpr char16_t kTruncationMarker = u'\u2026';

    explicit LogView(QWidget* parent = nullptr);

    // The view does not own the source; it is released when the source is destroyed.
    void setLineSource(model::LineSource* source);

    [[nodiscard]] model::LineSource* lineSource() const noexcept { return m_source; }

    [[nodiscard]] core::LineCount rowCount() const;

    // Top visible row; scrollToRow clamps so that the last page is full when possible.
    [[nodiscard]] core::LineNumber topRow() const noexcept { return m_topRow; }

    void scrollToRow(core::LineNumber row);

    // Current row (highlighted), or nullopt when there are no rows (F-VIEW-10).
    [[nodiscard]] std::optional<core::LineNumber> currentRow() const noexcept
    {
        return m_currentRow;
    }

    void setCurrentRow(core::LineNumber row);

    // Makes `row` current and scrolls so that it is visible (F-VIEW-6, F-VIEW-10).
    void moveCurrentRow(core::LineNumber row);

    // Scrolls the least amount so that `row` is visible.
    void ensureRowVisible(core::LineNumber row);

    // Makes `row` current and shows it in the middle of the view (go to line).
    void goToRow(core::LineNumber row);

    // Opens the "Go to line" dialog (Ctrl+G).
    void showGoToLineDialog();

    // Selection (SPEC F-VIEW-7/8/9). Columns are UTF-16 offsets into the decoded row text.
    [[nodiscard]] const TextSelection& selection() const noexcept { return m_selection; }

    void setSelection(const TextSelection& selection);
    void clearSelection();
    void selectAll();

    // The selected original text: rows separated by "\n", complete lines in line mode end with
    // "\n" (a partial last line of the file does not). Qt converts to "\r\n" on the Windows
    // clipboard.
    [[nodiscard]] QString selectedText() const;

    // Copies selectedText() to the clipboard (Ctrl+C).
    void copy() const;

    // Text position under a viewport point (clamped to existing rows and columns).
    [[nodiscard]] TextPosition positionAt(QPoint point) const;

    // Viewport point of a text position (vertical center of the row); for tests.
    [[nodiscard]] QPoint pointForPosition(TextPosition position) const;

    // Number of rows that fit completely into the viewport (at least 1).
    [[nodiscard]] std::uint64_t pageRows() const;

    void setLineNumbersVisible(bool visible);

    [[nodiscard]] bool lineNumbersVisible() const noexcept { return m_lineNumbersVisible; }

    void setTabWidth(int width);

    [[nodiscard]] int tabWidth() const noexcept { return m_tabWidth; }

    void setColumnLimit(int columns);

    [[nodiscard]] int columnLimit() const noexcept { return m_columnLimit; }

    // Horizontal scroll offset in pixels (F-VIEW-5).
    [[nodiscard]] int horizontalOffset() const;

    // The text painted for a decoded line: tabs expanded, cut at the column limit with a marker.
    [[nodiscard]] QString displayText(const QString& text, bool truncatedByReader) const;

    [[nodiscard]] int lineHeight() const noexcept { return m_lineHeight; }

    [[nodiscard]] int gutterWidth() const;

    // Duration of the last paint in microseconds (P-UI-1 check).
    [[nodiscard]] std::int64_t lastPaintMicroseconds() const noexcept { return m_lastPaintUs; }

signals:
    void topRowChanged(lv::core::LineNumber row);
    void currentRowChanged(lv::core::LineNumber row);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    // Display text of a row and the display offset of every original column (size + 1 entries).
    struct DisplayMapping
    {
        QString text;
        std::vector<qsizetype> displayOffsets;
    };

    [[nodiscard]] DisplayMapping mapLine(const QString& text, bool truncated) const;
    [[nodiscard]] QString rowText(std::uint64_t row) const;
    void updateSelectionAfterMove(bool extend, std::optional<core::LineNumber> before);
    void onRowsAppended();
    void onReset();
    void updateMetrics();
    void updateScrollBars();
    [[nodiscard]] std::uint64_t maxTopRow() const;
    [[nodiscard]] int scrollValueForRow(std::uint64_t row) const;
    [[nodiscard]] std::uint64_t rowForScrollValue(int value) const;

    QPointer<model::LineSource> m_source;
    core::LineNumber m_topRow;
    std::optional<core::LineNumber> m_currentRow;
    bool m_lineNumbersVisible = true;
    int m_tabWidth = kDefaultTabWidth;
    int m_columnLimit = kDefaultColumnLimit;
    int m_lineHeight = 1;
    int m_ascent = 0;
    int m_maxTextWidth = 0;  // widest painted line so far, for the horizontal range
    bool m_updatingScrollBars = false;
    int m_wheelRemainder = 0;  // angle delta not yet turned into rows (smooth wheels, touchpads)
    std::int64_t m_lastPaintUs = 0;
    TextSelection m_selection;
    bool m_dragging = false;
    QElapsedTimer m_tripleClickTimer;  // started on a double click
    std::uint64_t m_doubleClickRow = 0;
};

}  // namespace lv::ui
