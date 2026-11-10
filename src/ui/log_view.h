#pragma once

#include "core/util/strong_types.h"
#include "model/bookmarks.h"
#include "model/line_highlighter.h"
#include "model/line_source.h"
#include "model/mark_set.h"
#include "model/match_highlighter.h"
#include "ui/text_selection.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QProgressDialog>
#include <QRectF>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

class QPainter;

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
    LogView(const LogView&) = delete;
    LogView& operator=(const LogView&) = delete;
    LogView(LogView&&) = delete;
    LogView& operator=(LogView&&) = delete;
    ~LogView() override;

    // Size limits for copying (SPEC F-VIEW-8), in raw file bytes (see model::estimateCopyBytes).
    struct CopyLimits
    {
        // Larger copies run on a worker thread with a cancellable progress dialog.
        std::uint64_t backgroundBytes = std::uint64_t{4} * 1024 * 1024;
        // Larger copies ask for confirmation first.
        std::uint64_t confirmBytes = std::uint64_t{100} * 1024 * 1024;
    };

    // The view does not own the source; it is released when the source is destroyed.
    void setLineSource(model::LineSource* source);

    [[nodiscard]] model::LineSource* lineSource() const noexcept { return m_source; }

    // Match highlighting of the current filter (F-FILT-8); null = none. Not owned.
    void setHighlighter(const model::MatchHighlighter* highlighter);

    // Colors of the view (F-SET-4): a color set here overrides the default, which depends on
    // whether the palette is light or dark. An invalid color restores the default.
    [[nodiscard]] QColor matchHighlightColor() const;

    void setMatchHighlightColor(const QColor& color);

    // True if the palette's base color is dark (dark defaults are used).
    [[nodiscard]] bool usesDarkColors() const;
    [[nodiscard]] QColor bookmarkColor() const;

    // Viewport rectangles of the highlighted matches in a visible row (for tests).
    [[nodiscard]] std::vector<QRectF> matchRects(core::LineNumber row) const;

    // Marks of the document (F-MARK-1 … F-MARK-5); null = no mark column and no mark actions.
    // Not owned.
    void setMarks(model::MarkSet* marks);

    [[nodiscard]] model::MarkSet* marks() const noexcept { return m_marks; }

    // Toggles marks on the selected rows, or on the current row without a selection (M).
    void toggleMarks();
    // Marks the selected rows (or the current row) with color `color` (Ctrl+1 … Ctrl+9).
    void toggleMarkColor(int color);
    // Moves to the next / previous marked row (Ctrl+Down / Ctrl+Up); false if there is none.
    bool goToNextMark();
    bool goToPreviousMark();
    // Asks for confirmation (injectable for tests) and clears all marks (F-MARK-4).
    void clearMarks();

    void setClearMarksConfirmation(std::function<bool(std::size_t count)> confirm)
    {
        m_confirmClearMarks = std::move(confirm);
    }

    // Copies all marked lines in line order (F-MARK-5).
    void copyMarkedLines();
    // First row whose original line is >= `line` (rows are ascending), if any.
    [[nodiscard]] std::optional<core::LineNumber> rowAtOrAfter(core::LineNumber line) const;

    // Bookmarks of the document (F-BOOK-1 … F-BOOK-4); null = none. Not owned.
    void setBookmarks(model::Bookmarks* bookmarks);

    [[nodiscard]] model::Bookmarks* bookmarks() const noexcept { return m_bookmarks; }

    // Adds a bookmark on the current row (named after its text) or removes it (Ctrl+B).
    void toggleBookmark();
    // F2 / Shift+F2; false if there is none (the view only visits lines it shows).
    bool goToNextBookmark();
    bool goToPreviousBookmark();

    // Width of the mark column at the left edge of the gutter (0 without marks).
    [[nodiscard]] int markColumnWidth() const;

    [[nodiscard]] QColor markColor() const;

    // Default mark color (F-SET-1); the alpha also applies to the nine mark colors.
    void setMarkColor(const QColor& color);
    // Current line background; an invalid color derives it from the palette.
    void setCurrentLineColor(const QColor& color);
    [[nodiscard]] QColor currentLineColor() const;

    // Row background of mark color `index` (0 = default, F-MARK-6).
    [[nodiscard]] QColor markColor(int index) const;

    // Highlight rules of the tab (F-HL-5); null = none. Not owned.
    void setLineHighlighter(const model::LineHighlighter* highlighter);

    // Highlighting of the find bar's query (F-FIND-3), painted in its own color; null = none.
    void setFindHighlighter(const model::MatchHighlighter* highlighter);

    [[nodiscard]] QColor findHighlightColor() const;

    [[nodiscard]] std::vector<QRectF> findMatchRects(core::LineNumber row) const;

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

    // Follow mode (F-TAIL-3): the last row stays current and visible as rows are appended.
    // Turning it on jumps to the end. The user turns it off by scrolling or moving up and back
    // on with Ctrl+End; both emit followRequested() so that the owner can apply it per tab.
    void setFollowing(bool following);

    [[nodiscard]] bool isFollowing() const noexcept { return m_following; }

    // True while the view moves itself to the end (for listeners of currentRowChanged).
    [[nodiscard]] bool isAutoScrolling() const noexcept { return m_autoScrolling; }

    // Selection (SPEC F-VIEW-7/8/9). Columns are UTF-16 offsets into the decoded row text.
    [[nodiscard]] const TextSelection& selection() const noexcept { return m_selection; }

    void setSelection(const TextSelection& selection);
    void clearSelection();
    void selectAll();

    // The selected original text: rows separated by "\n", complete lines in line mode end with
    // "\n" (a partial last line of the file does not). Qt converts to "\r\n" on the Windows
    // clipboard.
    [[nodiscard]] QString selectedText() const;

    // Copies the selection to the clipboard (Ctrl+C). Large selections ask for confirmation and
    // are copied in the background; see CopyLimits.
    void copy();

    // Cancels a running background copy; the clipboard stays unchanged.
    void cancelCopy();

    [[nodiscard]] bool isCopying() const noexcept { return m_copyThread.joinable(); }

    void setCopyLimits(CopyLimits limits) { m_copyLimits = limits; }

    // Asked before copying more than CopyLimits::confirmBytes; returns true to continue.
    // The default shows a message box. Tests replace it.
    void setCopyConfirmation(std::function<bool(std::uint64_t bytes)> confirm)
    {
        m_confirmCopy = std::move(confirm);
    }

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
    // "Highlight this text" on a selection within one line (F-HL-7).
    void highlightTextRequested(const QString& text);
    void topRowChanged(lv::core::LineNumber row);
    void currentRowChanged(lv::core::LineNumber row);
    // The user asked to turn follow mode on (Ctrl+End) or off (scrolled or moved up).
    void followRequested(bool following);
    // A copy finished: `copied` is false when it was declined, canceled, or failed.
    void copyFinished(bool copied);

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
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Display text of a row and the display offset of every original column (size + 1 entries).
    struct DisplayMapping
    {
        QString text;
        std::vector<qsizetype> displayOffsets;
    };

    [[nodiscard]] DisplayMapping mapLine(const QString& text, bool truncated) const;
    [[nodiscard]] QString rowText(std::uint64_t row) const;
    // Highlight rectangles of one painted row at vertical position `y`.
    [[nodiscard]] std::vector<QRectF> highlightRects(const model::MatchHighlighter* highlighter,
                                                     std::uint64_t row,
                                                     const model::DisplayLine& line,
                                                     const DisplayMapping& mapping, int y) const;
    [[nodiscard]] std::vector<QRectF> visibleRects(const model::MatchHighlighter* highlighter,
                                                   core::LineNumber row) const;
    void startBackgroundCopy(model::RowReader reader, model::CopyRange range,
                             std::uint64_t totalBytes);
    void finishBackgroundCopy();
    void updateSelectionAfterMove(bool extend, std::optional<core::LineNumber> before);
    void onRowsAppended();
    void onReset();
    void onRowsAboutToChange();
    void onRowsChanged();
    // Position remembered across rowsAboutToChange()/rowsChanged().
    std::optional<core::LineNumber> m_keepCurrentLine;
    std::optional<core::LineNumber> m_keepTopLine;
    void updateMetrics();
    void updateScrollBars();
    [[nodiscard]] std::uint64_t maxTopRow() const;
    [[nodiscard]] int scrollValueForRow(std::uint64_t row) const;
    [[nodiscard]] std::uint64_t rowForScrollValue(int value) const;

    QPointer<model::LineSource> m_source;
    QPointer<const model::MatchHighlighter> m_highlighter;
    QColor m_matchColor;  // invalid = theme default (amber)
    QPointer<const model::MatchHighlighter> m_findHighlighter;
    QPointer<model::MarkSet> m_marks;
    QPointer<const model::LineHighlighter> m_lineHighlighter;
    // Rectangles of UTF-16 column range [from, to) of a painted row.
    [[nodiscard]] std::optional<QRectF> rangeRect(const DisplayMapping& mapping,
                                                  const model::HighlightRange& range, int y) const;
    QPointer<model::Bookmarks> m_bookmarks;
    // Text color that is readable on `background` (blended over the base color).
    [[nodiscard]] QColor readableTextOn(const QColor& background) const;
    [[nodiscard]] QColor textColorFor(const model::DisplayLine& line,
                                      const model::LineHighlight& rules) const;
    void paintRuleSpanBackgrounds(QPainter& painter, const model::LineHighlight& rules,
                                  const DisplayMapping& mapping, int y) const;
    void paintSelection(QPainter& painter, std::uint64_t row, const model::DisplayLine& line,
                        const DisplayMapping& mapping, int textX, int y) const;
    // Whole-line rule background and the current-line indicator.
    void paintRuleBackground(QPainter& painter, const model::LineHighlight& rules,
                             std::uint64_t row, int y) const;
    // Text of match-only rules with a foreground color.
    void paintRuleForegrounds(QPainter& painter, const model::LineHighlight& rules,
                              const DisplayMapping& mapping, double textX, int y) const;
    // Mark dot and bookmark ribbon in the mark column of `row` at height `y`.
    void paintMarkSymbols(QPainter& painter, core::LineNumber row, int y,
                          std::optional<int> markIndex) const;
    // Moves to the first line in `step(line)` order that this view shows.
    template <typename Step>
    bool goToLineBy(Step step);
    QColor m_markColor;         // invalid = theme default (blue row background)
    QColor m_currentLineColor;  // invalid = derived from the palette highlight
    std::function<bool(std::size_t)> m_confirmClearMarks;
    // Original line numbers of the selected rows (or the current row).
    [[nodiscard]] std::vector<core::LineNumber> targetLines() const;

    QColor m_findColor;  // invalid = theme default (blue, distinct from the filter highlight)
    core::LineNumber m_topRow;
    std::optional<core::LineNumber> m_currentRow;
    bool m_lineNumbersVisible = true;
    int m_tabWidth = kDefaultTabWidth;
    int m_columnLimit = kDefaultColumnLimit;
    int m_lineHeight = 1;
    int m_ascent = 0;
    int m_maxTextWidth = 0;  // widest painted line so far, for the horizontal range
    bool m_updatingScrollBars = false;
    bool m_following = false;
    bool m_autoScrolling = false;
    void followEnd();
    void userMovedUp();
    int m_wheelRemainder = 0;  // angle delta not yet turned into rows (smooth wheels, touchpads)
    std::int64_t m_lastPaintUs = 0;
    TextSelection m_selection;
    bool m_dragging = false;
    QElapsedTimer m_tripleClickTimer;  // started on a double click
    std::uint64_t m_doubleClickRow = 0;

    CopyLimits m_copyLimits;
    std::function<bool(std::uint64_t)> m_confirmCopy;
    std::atomic<std::uint64_t> m_copyProcessed{0};
    std::uint64_t m_copyTotal = 0;
    std::mutex m_copyMutex;
    std::optional<core::Result<QString>> m_copyResult;  // guarded by m_copyMutex
    QPointer<QProgressDialog> m_copyDialog;
    QTimer* m_copyTimer = nullptr;
    std::jthread m_copyThread;  // joined in the destructor before members are destroyed
};

}  // namespace lv::ui
