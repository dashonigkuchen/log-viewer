#include "ui/log_view.h"

#include "core/encoding/decode.h"
#include "core/util/assert.h"
#include "model/line_source.h"
#include "ui/go_to_line_dialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <stop_token>
#include <string_view>
#include <vector>

namespace lv::ui
{

namespace
{

constexpr int kGutterPadding = 6;
constexpr int kTextMargin = 4;

int digitCount(std::uint64_t value)
{
    int digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

}  // namespace

LogView::LogView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(true);
    viewport()->setBackgroundRole(QPalette::Base);

    auto* goToLine = new QAction(QStringLiteral("Go to Line..."), this);
    goToLine->setObjectName(QStringLiteral("actionGoToLine"));
    goToLine->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    goToLine->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(goToLine, &QAction::triggered, this, &LogView::showGoToLineDialog);
    addAction(goToLine);

    auto* copyAction = new QAction(QStringLiteral("Copy"), this);
    copyAction->setObjectName(QStringLiteral("actionCopy"));
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copyAction, &QAction::triggered, this, &LogView::copy);
    addAction(copyAction);

    auto* selectAllAction = new QAction(QStringLiteral("Select All"), this);
    selectAllAction->setObjectName(QStringLiteral("actionSelectAll"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    selectAllAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(selectAllAction, &QAction::triggered, this, &LogView::selectAll);
    addAction(selectAllAction);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    updateMetrics();
}

void LogView::setLineSource(model::LineSource* source)
{
    if (!m_source.isNull()) {
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = source;
    if (!m_source.isNull()) {
        connect(m_source, &model::LineSource::linesAppended, this, &LogView::onRowsAppended);
        connect(m_source, &model::LineSource::reset, this, &LogView::onReset);
        connect(m_source, &model::LineSource::rowsAboutToChange, this,
                &LogView::onRowsAboutToChange);
        connect(m_source, &model::LineSource::rowsChanged, this, &LogView::onRowsChanged);
    }
    onReset();
}

core::LineCount LogView::rowCount() const
{
    return m_source.isNull() ? core::LineCount(0) : m_source->lineCount();
}

std::uint64_t LogView::pageRows() const
{
    return static_cast<std::uint64_t>(std::max(1, viewport()->height() / m_lineHeight));
}

std::uint64_t LogView::maxTopRow() const
{
    const std::uint64_t rows = rowCount().get();
    const std::uint64_t page = pageRows();
    return rows > page ? rows - page : 0;
}

void LogView::scrollToRow(core::LineNumber row)
{
    const core::LineNumber clamped(std::min(row.get(), maxTopRow()));
    if (clamped != m_topRow) {
        m_topRow = clamped;
        emit topRowChanged(m_topRow);
    }
    updateScrollBars();
    viewport()->update();
}

void LogView::setFollowing(bool following)
{
    if (following == m_following) {
        return;
    }
    m_following = following;
    if (following) {
        followEnd();
    }
}

void LogView::followEnd()
{
    const std::uint64_t rows = rowCount().get();
    if (rows == 0) {
        return;
    }
    m_autoScrolling = true;
    setCurrentRow(core::LineNumber(rows - 1));
    scrollToRow(core::LineNumber(maxTopRow()));
    m_autoScrolling = false;
}

void LogView::userMovedUp()
{
    if (m_following) {
        emit followRequested(false);
    }
}

void LogView::setCurrentRow(core::LineNumber row)
{
    const std::uint64_t rows = rowCount().get();
    if (rows == 0) {
        return;
    }
    const core::LineNumber clamped(std::min(row.get(), rows - 1));
    if (m_currentRow != clamped) {
        m_currentRow = clamped;
        emit currentRowChanged(clamped);
        viewport()->update();
    }
}

void LogView::ensureRowVisible(core::LineNumber row)
{
    const std::uint64_t top = m_topRow.get();
    const std::uint64_t page = pageRows();
    if (row.get() < top) {
        scrollToRow(row);
    } else if (row.get() >= top + page) {
        scrollToRow(core::LineNumber(row.get() - page + 1));
    }
}

void LogView::moveCurrentRow(core::LineNumber row)
{
    const std::uint64_t rows = rowCount().get();
    if (rows == 0) {
        return;
    }
    const core::LineNumber clamped(std::min(row.get(), rows - 1));
    setCurrentRow(clamped);
    ensureRowVisible(clamped);
}

void LogView::goToRow(core::LineNumber row)
{
    const std::uint64_t rows = rowCount().get();
    if (rows == 0) {
        return;
    }
    const core::LineNumber clamped(std::min(row.get(), rows - 1));
    setCurrentRow(clamped);
    const std::uint64_t half = pageRows() / 2;
    scrollToRow(core::LineNumber(clamped.get() > half ? clamped.get() - half : 0));
}

void LogView::showGoToLineDialog()
{
    const std::uint64_t rows = rowCount().get();
    if (rows == 0) {
        return;
    }
    const std::uint64_t current = m_currentRow ? m_currentRow->get() + 1 : 1;
    GoToLineDialog dialog(rows, current, this);
    if (dialog.exec() == QDialog::Accepted) {
        if (const auto line = dialog.line()) {
            goToRow(core::LineNumber(*line - 1));
        }
    }
}

void LogView::keyPressEvent(QKeyEvent* event)
{
    const std::uint64_t rows = rowCount().get();
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    const std::uint64_t current = m_currentRow ? m_currentRow->get() : 0;
    const std::uint64_t page = pageRows();
    auto* horizontal = horizontalScrollBar();
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const std::optional<core::LineNumber> before = m_currentRow;
    bool movesRow = true;
    if (control && !shift && event->key() == Qt::Key_Down && !m_marks.isNull()) {
        goToNextMark();
        event->accept();
        return;
    }
    if (control && !shift && event->key() == Qt::Key_Up && !m_marks.isNull()) {
        goToPreviousMark();
        event->accept();
        return;
    }
    if (control && !shift && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9 &&
        !m_marks.isNull()) {
        toggleMarkColor(event->key() - Qt::Key_0);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_M && event->modifiers() == Qt::NoModifier && !m_marks.isNull()) {
        toggleMarks();
        event->accept();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Up:
        userMovedUp();
        moveCurrentRow(core::LineNumber(current > 0 ? current - 1 : 0));
        break;
    case Qt::Key_Down:
        moveCurrentRow(core::LineNumber(current + 1));
        break;
    case Qt::Key_PageUp:
        userMovedUp();
        moveCurrentRow(core::LineNumber(current > page ? current - page : 0));
        break;
    case Qt::Key_PageDown:
        moveCurrentRow(core::LineNumber(current + page));
        break;
    case Qt::Key_Home:
        if (control) {
            userMovedUp();
            moveCurrentRow(core::LineNumber(0));
        } else {
            movesRow = false;
        }
        horizontal->setValue(0);
        break;
    case Qt::Key_End:
        if (control) {
            moveCurrentRow(core::LineNumber(rows > 0 ? rows - 1 : 0));
            horizontal->setValue(0);
            if (!m_following) {
                emit followRequested(true);  // Ctrl+End turns follow mode on (F-TAIL-3)
            }
        } else {
            movesRow = false;
            horizontal->setValue(horizontal->maximum());
        }
        break;
    case Qt::Key_Left:
        movesRow = false;
        horizontal->setValue(horizontal->value() - horizontal->singleStep());
        break;
    case Qt::Key_Right:
        movesRow = false;
        horizontal->setValue(horizontal->value() + horizontal->singleStep());
        break;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    if (movesRow) {
        updateSelectionAfterMove(shift, before);
    }
    event->accept();
}

void LogView::updateSelectionAfterMove(bool extend, std::optional<core::LineNumber> before)
{
    if (!m_currentRow) {
        return;
    }
    if (!extend) {
        // Empty selection at the new row: a later Shift+click extends from its start.
        const TextPosition here{.row = m_currentRow->get(), .column = 0};
        setSelection(TextSelection::characters(here, here));
        return;
    }
    // Shift+navigation selects whole lines from where the keyboard selection started.
    const bool continues = m_selection.lineMode && !m_selection.isEmpty() && before &&
                           m_selection.cursor.row == before->get();
    const std::uint64_t anchor =
        continues ? m_selection.anchor.row : before.value_or(*m_currentRow).get();
    setSelection(TextSelection::lines(anchor, m_currentRow->get()));
}

void LogView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    event->accept();
    if (rowCount().get() == 0) {
        return;
    }
    const QPoint point = event->position().toPoint();
    const auto clickedRow =
        m_topRow.get() + static_cast<std::uint64_t>(std::max(0, point.y()) / m_lineHeight);
    if (clickedRow >= rowCount().get()) {
        clearSelection();
        return;  // below the last row: keep the current row
    }
    if (point.x() < markColumnWidth() && !m_marks.isNull()) {
        // Click in the mark column toggles the mark of that row (F-MARK-1).
        m_marks->toggle(m_source->originalLineNumber(core::LineNumber(clickedRow)));
        return;
    }
    const TextPosition position = positionAt(point);
    const bool tripleClick = m_tripleClickTimer.isValid() &&
                             m_tripleClickTimer.elapsed() < QApplication::doubleClickInterval() &&
                             position.row == m_doubleClickRow;
    m_tripleClickTimer.invalidate();
    if (tripleClick) {
        setSelection(TextSelection::lines(position.row, position.row));
    } else if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        // Extend from the last click position (an empty character selection keeps it), or
        // from the start of the current row after keyboard or line selections.
        TextPosition anchor = m_selection.anchor;
        if (m_selection.lineMode) {
            anchor = {.row = m_currentRow.value_or(core::LineNumber(0)).get(), .column = 0};
        }
        setSelection(TextSelection::characters(anchor, position));
        m_dragging = true;
    } else {
        setSelection(TextSelection::characters(position, position));
        m_dragging = true;
    }
    setCurrentRow(core::LineNumber(position.row));
}

void LogView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging || !event->buttons().testFlag(Qt::LeftButton)) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    const QPoint point = event->position().toPoint();
    // Dragging above or below the viewport scrolls one row per event.
    if (point.y() < 0 && m_topRow.get() > 0) {
        scrollToRow(core::LineNumber(m_topRow.get() - 1));
    } else if (point.y() >= viewport()->height()) {
        scrollToRow(core::LineNumber(m_topRow.get() + 1));
    }
    TextSelection selection = m_selection;
    selection.cursor = positionAt(point);
    setSelection(selection);
    setCurrentRow(core::LineNumber(selection.cursor.row));
    event->accept();
}

void LogView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void LogView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || rowCount().get() == 0) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }
    event->accept();
    m_dragging = false;
    const TextPosition position = positionAt(event->position().toPoint());
    m_doubleClickRow = position.row;
    m_tripleClickTimer.start();
    // Select the word under the click: letters, digits, and '_'; any other character alone.
    const QString text = rowText(position.row);
    if (text.isEmpty()) {
        return;
    }
    const qsizetype at = std::min(position.column, text.size() - 1);
    auto isWord = [](QChar c) {
        return c.isLetterOrNumber() || c == u'_';
    };
    qsizetype from = at;
    qsizetype to = at + 1;
    if (isWord(text[at])) {
        while (from > 0 && isWord(text[from - 1])) {
            --from;
        }
        while (to < text.size() && isWord(text[to])) {
            ++to;
        }
    }
    setSelection(TextSelection::characters({.row = position.row, .column = from},
                                           {.row = position.row, .column = to}));
}

void LogView::setSelection(const TextSelection& selection)
{
    if (selection != m_selection) {
        m_selection = selection;
        viewport()->update();
    }
}

void LogView::clearSelection()
{
    setSelection(TextSelection{});
}

void LogView::selectAll()
{
    const std::uint64_t rows = rowCount().get();
    if (rows > 0) {
        setSelection(TextSelection::lines(0, rows - 1));
    }
}

QString LogView::rowText(std::uint64_t row) const
{
    if (m_source.isNull() || row >= rowCount().get()) {
        return {};
    }
    auto lines = m_source->plainLines(core::LineNumber(row), core::LineCount(1));
    return lines.empty() ? QString() : std::move(lines.front().text);
}

QString LogView::selectedText() const
{
    const std::uint64_t rows = rowCount().get();
    if (m_selection.isEmpty() || m_source.isNull() || rows == 0) {
        return {};
    }
    const std::uint64_t first = m_selection.start().row;
    const std::uint64_t last = std::min(m_selection.end().row, rows - 1);
    if (first > last) {
        return {};
    }
    constexpr std::uint64_t kChunkRows = 4096;
    QString out;
    for (std::uint64_t chunk = first; chunk <= last; chunk += kChunkRows) {
        const std::uint64_t count = std::min(kChunkRows, last - chunk + 1);
        const auto lines = m_source->plainLines(core::LineNumber(chunk), core::LineCount(count));
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];
            const auto columns = m_selection.columnsForRow(chunk + i, line.text.size());
            if (!columns) {
                continue;
            }
            out += QStringView(line.text).mid(columns->from, columns->to - columns->from);
            if (columns->lineEnd && !line.partial) {
                out += u'\n';
            }
        }
    }
    return out;
}

LogView::~LogView()
{
    // Stop a background copy without emitting signals from the destructor.
    if (m_copyThread.joinable()) {
        m_copyThread.request_stop();
        m_copyThread.join();
    }
}

void LogView::copy()
{
    if (isCopying() || m_selection.isEmpty() || m_source.isNull() || rowCount().get() == 0) {
        return;
    }
    const auto reader = m_source->rowReader();
    if (!reader) {
        const QString text = selectedText();
        if (!text.isEmpty()) {
            QApplication::clipboard()->setText(text);
        }
        emit copyFinished(!text.isEmpty());
        return;
    }
    const std::uint64_t rows = rowCount().get();
    model::CopyRange range;
    range.firstRow = m_selection.start().row;
    range.lastRow = std::min(m_selection.end().row, rows - 1);
    if (!m_selection.lineMode) {
        range.firstColumn = m_selection.start().column;
        range.lastColumn = m_selection.end().column;
    }
    const std::uint64_t bytes = model::estimateCopyBytes(*reader, range);
    if (bytes > m_copyLimits.confirmBytes) {
        const bool confirmed =
            m_confirmCopy
                ? m_confirmCopy(bytes)
                : QMessageBox::question(
                      this, QStringLiteral("Copy"),
                      QStringLiteral("The selection is about %1 MiB. Copy it to the clipboard?")
                          .arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 0)) ==
                      QMessageBox::Yes;
        if (!confirmed) {
            emit copyFinished(false);
            return;
        }
    }
    if (bytes <= m_copyLimits.backgroundBytes) {
        const auto text = model::extractText(*reader, range, std::stop_token{});
        const bool copied = text.has_value() && !text->isEmpty();
        if (copied) {
            QApplication::clipboard()->setText(*text);
        }
        emit copyFinished(copied);
        return;
    }
    startBackgroundCopy(*reader, range, bytes);
}

void LogView::startBackgroundCopy(model::RowReader reader, model::CopyRange range,
                                  std::uint64_t totalBytes)
{
    m_copyTotal = std::max<std::uint64_t>(totalBytes, 1);
    m_copyProcessed.store(0, std::memory_order_relaxed);
    {
        const std::scoped_lock lock(m_copyMutex);
        m_copyResult.reset();
    }
    m_copyDialog = new QProgressDialog(QStringLiteral("Copying selection..."),
                                       QStringLiteral("Cancel"), 0, 1000, this);
    m_copyDialog->setObjectName(QStringLiteral("copyProgressDialog"));
    m_copyDialog->setWindowModality(Qt::WindowModal);
    m_copyDialog->setMinimumDuration(300);
    m_copyDialog->setAutoClose(false);
    m_copyDialog->setAutoReset(false);
    connect(m_copyDialog, &QProgressDialog::canceled, this, &LogView::cancelCopy);

    if (m_copyTimer == nullptr) {
        m_copyTimer = new QTimer(this);
        m_copyTimer->setInterval(50);
        connect(m_copyTimer, &QTimer::timeout, this, &LogView::finishBackgroundCopy);
    }
    m_copyTimer->start();

    m_copyThread =
        std::jthread([this, reader = std::move(reader), range](const std::stop_token& stop) {
            auto result = model::extractText(reader, range, stop, [this](std::uint64_t processed) {
                m_copyProcessed.store(processed, std::memory_order_relaxed);
            });
            const std::scoped_lock lock(m_copyMutex);
            m_copyResult = std::move(result);
        });
}

void LogView::finishBackgroundCopy()
{
    // Polled by m_copyTimer on the UI thread: updates progress and finishes when the worker did.
    if (!m_copyDialog.isNull()) {
        const auto processed = m_copyProcessed.load(std::memory_order_relaxed);
        m_copyDialog->setValue(
            static_cast<int>(std::min<std::uint64_t>(1000, processed * 1000 / m_copyTotal)));
    }
    std::optional<core::Result<QString>> result;
    {
        const std::scoped_lock lock(m_copyMutex);
        result.swap(m_copyResult);
    }
    if (!result) {
        return;
    }
    m_copyThread.join();
    m_copyThread = {};
    m_copyTimer->stop();
    if (!m_copyDialog.isNull()) {
        m_copyDialog->deleteLater();
    }
    const bool copied = result->has_value() && !(*result)->isEmpty();
    if (copied) {
        QApplication::clipboard()->setText(**result);
    }
    emit copyFinished(copied);
}

void LogView::cancelCopy()
{
    if (!m_copyThread.joinable()) {
        return;
    }
    m_copyThread.request_stop();
    m_copyThread.join();
    m_copyThread = {};
    {
        const std::scoped_lock lock(m_copyMutex);
        m_copyResult.reset();
    }
    if (m_copyTimer != nullptr) {
        m_copyTimer->stop();
    }
    if (!m_copyDialog.isNull()) {
        m_copyDialog->deleteLater();
    }
    emit copyFinished(false);
}

LogView::DisplayMapping LogView::mapLine(const QString& text, bool truncated) const
{
    DisplayMapping mapping;
    const auto limit = static_cast<qsizetype>(m_columnLimit);
    mapping.displayOffsets.reserve(static_cast<std::size_t>(text.size()) + 1);
    mapping.text.reserve(std::min(text.size(), limit) + 1);
    qsizetype column = 0;
    bool cut = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        mapping.displayOffsets.push_back(std::min(mapping.text.size(), limit));
        if (cut) {
            continue;
        }
        const QChar c = text[i];
        if (c == u'\t') {
            const qsizetype next = ((column / m_tabWidth) + 1) * m_tabWidth;
            mapping.text.append(QString(next - column, u' '));
            column = next;
        } else {
            mapping.text.append(c);
            if (!c.isLowSurrogate()) {
                ++column;
            }
        }
        if (mapping.text.size() >= limit) {
            cut = mapping.text.size() > limit || i + 1 < text.size();
            mapping.text.truncate(limit);
        }
    }
    mapping.displayOffsets.push_back(std::min(mapping.text.size(), limit));
    if (cut || truncated) {
        mapping.text.append(QChar(kTruncationMarker));
    }
    return mapping;
}

TextPosition LogView::positionAt(QPoint point) const
{
    const std::uint64_t rows = rowCount().get();
    if (rows == 0) {
        return {};
    }
    const std::uint64_t offset = static_cast<std::uint64_t>(std::max(0, point.y())) /
                                 static_cast<std::uint64_t>(m_lineHeight);
    const std::uint64_t row = std::min(m_topRow.get() + offset, rows - 1);
    const auto lines = m_source->lines(core::LineNumber(row), core::LineCount(1));
    const QString& text = lines.front().text;
    const DisplayMapping mapping = mapLine(text, lines.front().truncated);
    const double x = point.x() - gutterWidth() - kTextMargin + horizontalOffset();
    if (x <= 0) {
        return {.row = row, .column = 0};
    }
    // Nearest original column boundary by its painted x position (binary search on prefixes).
    const QFontMetricsF metrics(font());
    auto xOf = [&](qsizetype column) {
        return metrics.horizontalAdvance(
            mapping.text.left(mapping.displayOffsets[static_cast<std::size_t>(column)]));
    };
    qsizetype low = 0;
    qsizetype high = text.size();
    while (low < high) {
        const qsizetype middle = low + ((high - low) / 2);
        if (xOf(middle) < x) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low > 0 && (x - xOf(low - 1)) < (xOf(low) - x)) {
        --low;
    }
    if (low > 0 && low < text.size() && text[low].isLowSurrogate()) {
        --low;  // never between the halves of a surrogate pair
    }
    return {.row = row, .column = low};
}

void LogView::setHighlighter(const model::MatchHighlighter* highlighter)
{
    if (!m_highlighter.isNull()) {
        disconnect(m_highlighter, nullptr, this, nullptr);
    }
    m_highlighter = highlighter;
    if (highlighter != nullptr) {
        connect(highlighter, &model::MatchHighlighter::changed, viewport(),
                qOverload<>(&QWidget::update));
    }
    viewport()->update();
}

void LogView::setMatchHighlightColor(const QColor& color)
{
    m_matchColor = color;
    viewport()->update();
}

void LogView::setFindHighlighter(const model::MatchHighlighter* highlighter)
{
    if (!m_findHighlighter.isNull()) {
        disconnect(m_findHighlighter, nullptr, this, nullptr);
        disconnect(m_findHighlighter, nullptr, viewport(), nullptr);
    }
    m_findHighlighter = highlighter;
    if (highlighter != nullptr) {
        connect(highlighter, &model::MatchHighlighter::changed, viewport(),
                qOverload<>(&QWidget::update));
    }
    viewport()->update();
}

std::vector<QRectF> LogView::highlightRects(const model::MatchHighlighter* highlighter,
                                            std::uint64_t row, const model::DisplayLine& line,
                                            const DisplayMapping& mapping, int y) const
{
    std::vector<QRectF> rects;
    if (highlighter == nullptr || line.unavailable) {
        return rects;
    }
    const auto ranges = highlighter->ranges(m_source->originalLineNumber(core::LineNumber(row)),
                                            line.text, line.partial);
    if (ranges.empty()) {
        return rects;
    }
    const QFontMetricsF metrics(font());
    const double textX = gutterWidth() + kTextMargin - horizontalOffset();
    for (const auto& range : ranges) {
        const auto from = mapping.displayOffsets[static_cast<std::size_t>(range.from)];
        const auto to = mapping.displayOffsets[static_cast<std::size_t>(range.to)];
        if (to <= from) {
            continue;  // beyond the column limit
        }
        const double left = textX + metrics.horizontalAdvance(mapping.text.left(from));
        const double right = textX + metrics.horizontalAdvance(mapping.text.left(to));
        rects.emplace_back(left, y, right - left, m_lineHeight);
    }
    return rects;
}

std::vector<QRectF> LogView::visibleRects(const model::MatchHighlighter* highlighter,
                                          core::LineNumber row) const
{
    if (m_source.isNull() || row < m_topRow || row.get() >= rowCount().get()) {
        return {};
    }
    const auto lines = m_source->lines(row, core::LineCount(1));
    const DisplayMapping mapping = mapLine(lines.front().text, lines.front().truncated);
    const auto offset = static_cast<int>(row.get() - m_topRow.get());
    return highlightRects(highlighter, row.get(), lines.front(), mapping, offset * m_lineHeight);
}

std::vector<QRectF> LogView::matchRects(core::LineNumber row) const
{
    return visibleRects(m_highlighter.data(), row);
}

std::vector<QRectF> LogView::findMatchRects(core::LineNumber row) const
{
    return visibleRects(m_findHighlighter.data(), row);
}

QPoint LogView::pointForPosition(TextPosition position) const
{
    const auto lines = m_source->lines(core::LineNumber(position.row), core::LineCount(1));
    const DisplayMapping mapping = mapLine(lines.front().text, lines.front().truncated);
    const QFontMetricsF metrics(font());
    const auto column = std::min<qsizetype>(position.column, lines.front().text.size());
    const double x = metrics.horizontalAdvance(
        mapping.text.left(mapping.displayOffsets[static_cast<std::size_t>(column)]));
    const auto rowOffset = static_cast<int>(position.row - m_topRow.get());
    return {gutterWidth() + kTextMargin - horizontalOffset() + static_cast<int>(std::lround(x)),
            (rowOffset * m_lineHeight) + (m_lineHeight / 2)};
}

void LogView::wheelEvent(QWheelEvent* event)
{
    // Horizontal wheels and Shift+wheel scroll sideways (handled by the scroll area).
    const QPoint angle = event->angleDelta();
    if (angle.y() == 0 || event->modifiers().testFlag(Qt::ShiftModifier)) {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }
    // Rows are counted from the angle in 1/8 degree units (120 per notch), using the system's
    // lines-per-notch setting; fractions of a notch accumulate (smooth wheels, touchpads).
    constexpr int kNotch = 120;
    const int linesPerNotch = std::max(1, QApplication::wheelScrollLines());
    m_wheelRemainder += angle.y() * linesPerNotch;
    const int rows = m_wheelRemainder / kNotch;
    m_wheelRemainder -= rows * kNotch;
    if (rows != 0) {
        const std::uint64_t top = m_topRow.get();
        const auto steps = static_cast<std::uint64_t>(std::abs(rows));
        if (rows > 0) {
            userMovedUp();
            scrollToRow(core::LineNumber(top > steps ? top - steps : 0));
        } else {
            scrollToRow(core::LineNumber(top + steps));
        }
    }
    event->accept();
}

void LogView::setLineNumbersVisible(bool visible)
{
    m_lineNumbersVisible = visible;
    updateScrollBars();
    viewport()->update();
}

void LogView::setTabWidth(int width)
{
    LV_ASSERT(width > 0);
    m_tabWidth = width;
    m_maxTextWidth = 0;
    viewport()->update();
}

void LogView::setColumnLimit(int columns)
{
    LV_ASSERT(columns > 0);
    m_columnLimit = columns;
    m_maxTextWidth = 0;
    viewport()->update();
}

int LogView::horizontalOffset() const
{
    return horizontalScrollBar()->value();
}

int LogView::markColumnWidth() const
{
    return m_marks.isNull() && m_bookmarks.isNull() ? 0 : std::max(8, (m_lineHeight * 3) / 4);
}

int LogView::gutterWidth() const
{
    if (!m_lineNumbersVisible) {
        return markColumnWidth();
    }
    const QFontMetricsF metrics(font());
    // Rows show original line numbers; the last row has the largest one (ascending sources).
    const std::uint64_t rows = rowCount().get();
    const std::uint64_t largest =
        rows == 0 ? 1 : m_source->originalLineNumber(core::LineNumber(rows - 1)).get() + 1;
    const int digits = digitCount(largest);
    return markColumnWidth() +
           static_cast<int>(std::ceil(metrics.horizontalAdvance(QString(digits, u'9')))) +
           (2 * kGutterPadding);
}

void LogView::setMarks(model::MarkSet* marks)
{
    if (!m_marks.isNull()) {
        disconnect(m_marks, nullptr, this, nullptr);
        disconnect(m_marks, nullptr, viewport(), nullptr);
    }
    m_marks = marks;
    if (marks != nullptr) {
        connect(marks, &model::MarkSet::changed, viewport(), qOverload<>(&QWidget::update));
    }
    updateScrollBars();
    viewport()->update();
}

std::vector<core::LineNumber> LogView::targetLines() const
{
    std::vector<core::LineNumber> lines;
    const std::uint64_t rows = rowCount().get();
    if (m_source.isNull() || rows == 0) {
        return lines;
    }
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    if (!m_selection.isEmpty()) {
        first = m_selection.start().row;
        last = m_selection.end().row;
        // A character selection that ends at column 0 does not include that row.
        if (!m_selection.lineMode && last > first && m_selection.end().column == 0) {
            --last;
        }
    } else if (m_currentRow) {
        first = last = m_currentRow->get();
    } else {
        return lines;
    }
    last = std::min(last, rows - 1);
    for (std::uint64_t row = first; row <= last; ++row) {
        lines.push_back(m_source->originalLineNumber(core::LineNumber(row)));
    }
    return lines;
}

QColor LogView::markColor(int index) const
{
    // Distinct hues for Ctrl+1 … Ctrl+9, same translucency as the default color.
    static constexpr std::array<QRgb, 9> kColors{0xE53935, 0xFB8C00, 0xFDD835, 0x43A047, 0x00ACC1,
                                                 0x1E88E5, 0x8E24AA, 0xD81B60, 0x6D4C41};
    if (index <= 0 || index > static_cast<int>(kColors.size())) {
        return m_markColor;
    }
    QColor color(kColors.at(static_cast<std::size_t>(index - 1)));
    color.setAlpha(m_markColor.alpha());
    return color;
}

void LogView::toggleMarkColor(int color)
{
    if (m_marks.isNull()) {
        return;
    }
    const auto lines = targetLines();
    m_marks->toggleColor(lines, color);
}

void LogView::toggleMarks()
{
    if (m_marks.isNull()) {
        return;
    }
    const auto lines = targetLines();
    m_marks->toggle(lines);
}

std::optional<core::LineNumber> LogView::rowAtOrAfter(core::LineNumber line) const
{
    // Rows map to ascending original lines: binary search.
    std::uint64_t low = 0;
    std::uint64_t high = rowCount().get();
    while (low < high) {
        const std::uint64_t mid = low + ((high - low) / 2);
        if (m_source->originalLineNumber(core::LineNumber(mid)) < line) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low < rowCount().get() ? std::optional(core::LineNumber(low)) : std::nullopt;
}

template <typename Step>
bool LogView::goToLineBy(Step step)
{
    if (m_source.isNull() || rowCount().get() == 0) {
        return false;
    }
    const std::uint64_t current = m_currentRow ? m_currentRow->get() : 0;
    auto line = m_source->originalLineNumber(core::LineNumber(current));
    // The target may be on a line this view does not show (filtered view): skip it.
    while (const std::optional<core::LineNumber> target = step(line)) {
        if (const auto row = rowAtOrAfter(*target);
            row && m_source->originalLineNumber(*row) == *target) {
            userMovedUp();  // leaves follow mode like any manual move
            goToRow(*row);
            return true;
        }
        line = *target;
    }
    return false;
}

bool LogView::goToNextMark()
{
    return !m_marks.isNull() &&
           goToLineBy([this](core::LineNumber line) { return m_marks->next(line); });
}

bool LogView::goToPreviousMark()
{
    return !m_marks.isNull() &&
           goToLineBy([this](core::LineNumber line) { return m_marks->previous(line); });
}

void LogView::setBookmarks(model::Bookmarks* bookmarks)
{
    if (!m_bookmarks.isNull()) {
        disconnect(m_bookmarks, nullptr, viewport(), nullptr);
    }
    m_bookmarks = bookmarks;
    if (bookmarks != nullptr) {
        connect(bookmarks, &model::Bookmarks::changed, viewport(), qOverload<>(&QWidget::update));
    }
    updateScrollBars();
    viewport()->update();
}

void LogView::toggleBookmark()
{
    if (m_bookmarks.isNull() || m_source.isNull() || !m_currentRow ||
        m_currentRow->get() >= rowCount().get()) {
        return;
    }
    const auto lines = m_source->plainLines(*m_currentRow, core::LineCount(1));
    const QString text = lines.empty() ? QString() : lines.front().text;
    m_bookmarks->toggle(m_source->originalLineNumber(*m_currentRow), text);
}

bool LogView::goToNextBookmark()
{
    return !m_bookmarks.isNull() &&
           goToLineBy([this](core::LineNumber line) { return m_bookmarks->next(line); });
}

bool LogView::goToPreviousBookmark()
{
    return !m_bookmarks.isNull() &&
           goToLineBy([this](core::LineNumber line) { return m_bookmarks->previous(line); });
}

void LogView::clearMarks()
{
    if (m_marks.isNull() || m_marks->empty()) {
        return;
    }
    const std::size_t count = m_marks->size();
    const bool confirmed =
        m_confirmClearMarks
            ? m_confirmClearMarks(count)
            : QMessageBox::question(this, QStringLiteral("Clear Marks"),
                                    QStringLiteral("Remove all %1 marks?").arg(count)) ==
                  QMessageBox::Yes;
    if (confirmed) {
        m_marks->clear();
    }
}

void LogView::copyMarkedLines()
{
    if (m_marks.isNull() || m_marks->empty() || m_source.isNull()) {
        return;
    }
    // The marks of the whole document, whatever this view shows; read through the document's
    // reader (rows = marks in line order).
    auto reader = m_source->rowReader();
    if (!reader) {
        return;
    }
    const auto lines = m_marks->lines();
    const std::uint64_t total = reader->snapshot.lineCount().get();
    std::vector<core::LineNumber> existing;
    std::ranges::copy_if(lines, std::back_inserter(existing),
                         [total](core::LineNumber line) { return line.get() < total; });
    if (existing.empty()) {
        return;
    }
    reader->lineForRow = [existing](core::LineNumber row) {
        return existing[static_cast<std::size_t>(row.get())];
    };
    reader->contiguous = false;
    const model::CopyRange range{.firstRow = 0,
                                 .lastRow = existing.size() - 1,
                                 .firstColumn = 0,
                                 .lastColumn = std::nullopt};
    const auto text = model::extractText(*reader, range, std::stop_token{});
    if (text && !text->isEmpty()) {
        QApplication::clipboard()->setText(*text);
    }
}

void LogView::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    if (!m_marks.isNull()) {
        QAction* toggle =
            menu.addAction(QStringLiteral("Toggle Mark"), this, &LogView::toggleMarks);
        toggle->setShortcut(QKeySequence(Qt::Key_M));
        toggle->setEnabled(rowCount().get() > 0);
        menu.addAction(QStringLiteral("Copy Marked Lines"), this, &LogView::copyMarkedLines)
            ->setEnabled(!m_marks->empty());
        menu.addAction(QStringLiteral("Clear All Marks..."), this, &LogView::clearMarks)
            ->setEnabled(!m_marks->empty());
        menu.addSeparator();
    }
    if (!m_bookmarks.isNull()) {
        QAction* bookmark =
            menu.addAction(QStringLiteral("Toggle Bookmark"), this, &LogView::toggleBookmark);
        bookmark->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
        bookmark->setEnabled(m_currentRow.has_value());
        menu.addSeparator();
    }
    menu.addAction(QStringLiteral("Copy"), this, &LogView::copy)
        ->setEnabled(!m_selection.isEmpty());
    menu.addAction(QStringLiteral("Select All"), this, &LogView::selectAll);
    menu.exec(event->globalPos());
}

QString LogView::displayText(const QString& text, bool truncatedByReader) const
{
    return mapLine(text, truncatedByReader).text;
}

void LogView::updateMetrics()
{
    const QFontMetricsF metrics(font());
    m_lineHeight = std::max(1, static_cast<int>(std::ceil(metrics.height())));
    m_ascent = static_cast<int>(std::ceil(metrics.ascent()));
    m_maxTextWidth = 0;
    updateScrollBars();
}

int LogView::scrollValueForRow(std::uint64_t row) const
{
    const std::uint64_t maxTop = maxTopRow();
    if (maxTop <= static_cast<std::uint64_t>(kMaxScrollRange)) {
        return static_cast<int>(row);
    }
    const long double scaled =
        static_cast<long double>(row) * kMaxScrollRange / static_cast<long double>(maxTop);
    return static_cast<int>(std::llround(scaled));
}

std::uint64_t LogView::rowForScrollValue(int value) const
{
    const std::uint64_t maxTop = maxTopRow();
    if (maxTop <= static_cast<std::uint64_t>(kMaxScrollRange)) {
        return static_cast<std::uint64_t>(std::max(0, value));
    }
    const long double scaled = static_cast<long double>(std::max(0, value)) *
                               static_cast<long double>(maxTop) / kMaxScrollRange;
    return std::min(maxTop, static_cast<std::uint64_t>(std::llround(scaled)));
}

void LogView::updateScrollBars()
{
    m_updatingScrollBars = true;
    const std::uint64_t maxTop = maxTopRow();
    auto* vertical = verticalScrollBar();
    vertical->setRange(0, maxTop <= static_cast<std::uint64_t>(kMaxScrollRange)
                              ? static_cast<int>(maxTop)
                              : kMaxScrollRange);
    vertical->setPageStep(static_cast<int>(std::min<std::uint64_t>(pageRows(), kMaxScrollRange)));
    vertical->setSingleStep(1);
    vertical->setValue(scrollValueForRow(m_topRow.get()));

    const int textArea = std::max(0, viewport()->width() - gutterWidth() - kTextMargin);
    auto* horizontal = horizontalScrollBar();
    horizontal->setRange(0, std::max(0, m_maxTextWidth - textArea));
    horizontal->setPageStep(std::max(1, textArea));
    horizontal->setSingleStep(std::max(1, m_lineHeight));
    m_updatingScrollBars = false;
}

void LogView::scrollContentsBy(int /*dx*/, int dy)
{
    if (m_updatingScrollBars) {
        return;
    }
    if (dy > 0) {
        userMovedUp();  // the scroll bar moved up
    }
    if (dy != 0) {
        const core::LineNumber row(rowForScrollValue(verticalScrollBar()->value()));
        if (row != m_topRow) {
            m_topRow = row;
            emit topRowChanged(m_topRow);
        }
    }
    viewport()->update();
}

void LogView::onRowsAppended()
{
    if (!m_currentRow && rowCount().get() > 0) {
        m_currentRow = core::LineNumber(0);
    }
    updateScrollBars();
    viewport()->update();
    if (m_following) {
        followEnd();
    }
}

void LogView::onRowsAboutToChange()
{
    m_keepCurrentLine.reset();
    m_keepTopLine.reset();
    const std::uint64_t rows = rowCount().get();
    if (m_currentRow && m_currentRow->get() < rows) {
        m_keepCurrentLine = m_source->originalLineNumber(*m_currentRow);
    }
    if (m_topRow.get() < rows) {
        m_keepTopLine = m_source->originalLineNumber(m_topRow);
    }
}

void LogView::onRowsChanged()
{
    // Rows moved: a selection in row coordinates would now cover other lines.
    m_selection = TextSelection{};
    m_dragging = false;
    const std::uint64_t rows = rowCount().get();
    if (rows == 0) {
        m_currentRow.reset();
        m_topRow = core::LineNumber(0);
    } else {
        const auto restore = [&](std::optional<core::LineNumber> line) {
            if (!line) {
                return core::LineNumber(0);
            }
            return rowAtOrAfter(*line).value_or(core::LineNumber(rows - 1));
        };
        const core::LineNumber current = restore(m_keepCurrentLine);
        const core::LineNumber top = restore(m_keepTopLine);
        m_topRow = core::LineNumber(std::min(top.get(), maxTopRow()));
        if (m_currentRow != current) {
            m_currentRow = current;  // same line as before: no currentRowChanged (no view sync)
        }
    }
    updateScrollBars();
    viewport()->update();
    if (m_following) {
        followEnd();
    }
}

void LogView::onReset()
{
    m_topRow = core::LineNumber(0);
    m_currentRow.reset();
    m_selection = TextSelection{};
    m_dragging = false;
    m_maxTextWidth = 0;
    if (rowCount().get() > 0) {
        m_currentRow = core::LineNumber(0);
    }
    updateScrollBars();
    viewport()->update();
    if (m_following) {
        followEnd();
    }
}

void LogView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    // Keep the top row valid when the page grows (clamping to the new last page).
    m_topRow = core::LineNumber(std::min(m_topRow.get(), maxTopRow()));
    updateScrollBars();
}

void LogView::changeEvent(QEvent* event)
{
    QAbstractScrollArea::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        updateMetrics();
        viewport()->update();
    }
}

void LogView::paintMarkSymbols(QPainter& painter, core::LineNumber row, int y,
                               std::optional<int> markIndex) const
{
    const int column = markColumnWidth();
    if (markIndex) {
        // Mark symbol: a filled circle in the mark column (F-MARK-2).
        const QColor color = markColor(*markIndex);
        const int size = std::max(4, column - 4);
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(color.red(), color.green(), color.blue()));
        painter.drawEllipse(
            QRect((column - size) / 2, y + ((m_lineHeight - size) / 2), size, size));
        painter.restore();
    }
    if (!m_bookmarks.isNull() && m_bookmarks->contains(m_source->originalLineNumber(row))) {
        // Bookmark symbol: a ribbon in the mark column (F-BOOK-4).
        const int w = std::max(4, column - 4);
        const int left = (column - w) / 2;
        const int top = y + 1;
        const int bottom = y + m_lineHeight - 2;
        const std::array<QPoint, 5> ribbon{
            QPoint(left, top), QPoint(left + w, top), QPoint(left + w, bottom),
            QPoint(left + (w / 2), bottom - (w / 2)), QPoint(left, bottom)};
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Base), 1));
        painter.setBrush(m_bookmarkColor);
        painter.drawPolygon(ribbon.data(), static_cast<int>(ribbon.size()));
        painter.restore();
    }
}

void LogView::paintEvent(QPaintEvent* /*event*/)
{
    QElapsedTimer timer;
    timer.start();
    QPainter painter(viewport());
    const QPalette& colors = palette();
    const int width = viewport()->width();
    const int height = viewport()->height();
    const int gutter = gutterWidth();

    const std::uint64_t rows = rowCount().get();
    const std::uint64_t first = m_topRow.get();
    const std::uint64_t visible =
        first >= rows ? 0
                      : std::min<std::uint64_t>(
                            rows - first, static_cast<std::uint64_t>(height / m_lineHeight) + 1);

    if (gutter > 0) {
        painter.fillRect(0, 0, gutter, height, colors.alternateBase());
    }
    if (visible > 0) {
        const auto lines = m_source->lines(core::LineNumber(first), core::LineCount(visible));
        const int textX = gutter + kTextMargin - horizontalOffset();
        const QFontMetricsF metrics(font());
        int widest = m_maxTextWidth;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::uint64_t row = first + i;
            const int y = static_cast<int>(i) * m_lineHeight;
            const std::optional<int> markIndex =
                m_marks.isNull()
                    ? std::nullopt
                    : m_marks->colorOf(m_source->originalLineNumber(core::LineNumber(row)));
            const bool marked = markIndex.has_value();
            const QColor rowMarkColor = markColor(markIndex.value_or(0));
            if (m_currentRow && m_currentRow->get() == row) {
                painter.fillRect(gutter, y, width - gutter, m_lineHeight,
                                 colors.color(QPalette::Highlight).lighter(170));
            }
            if (marked) {
                painter.fillRect(gutter, y, width - gutter, m_lineHeight, rowMarkColor);
            }
            paintMarkSymbols(painter, core::LineNumber(row), y, markIndex);
            const auto& line = lines[i];
            const DisplayMapping mapping = mapLine(line.text, line.truncated);
            const QString& text = mapping.text;
            QColor textColor =
                colors.color(line.unavailable ? QPalette::PlaceholderText : QPalette::Text);
            if (line.extra) {
                // Marked line that does not match (F-FILT-9): dimmed text and a bar at the left.
                const QColor base = colors.color(QPalette::Base);
                textColor.setRgbF((textColor.redF() * 0.6F) + (base.redF() * 0.4F),
                                  (textColor.greenF() * 0.6F) + (base.greenF() * 0.4F),
                                  (textColor.blueF() * 0.6F) + (base.blueF() * 0.4F));
                painter.fillRect(
                    gutter, y, 3, m_lineHeight,
                    QColor(rowMarkColor.red(), rowMarkColor.green(), rowMarkColor.blue()));
            }
            painter.setPen(textColor);
            painter.setClipRect(gutter, 0, width - gutter, height);
            for (const QRectF& rect : highlightRects(m_highlighter.data(), row, line, mapping, y)) {
                painter.fillRect(rect, m_matchColor);
            }
            for (const QRectF& rect :
                 highlightRects(m_findHighlighter.data(), row, line, mapping, y)) {
                painter.fillRect(rect, m_findColor);
            }
            painter.drawText(textX, y + m_ascent, text);
            if (const auto selected = m_selection.columnsForRow(row, line.text.size())) {
                // Selected part: highlight background and redraw the text in the highlight color.
                const auto from = mapping.displayOffsets[static_cast<std::size_t>(selected->from)];
                const auto to = mapping.displayOffsets[static_cast<std::size_t>(selected->to)];
                const double left = textX + metrics.horizontalAdvance(text.left(from));
                double right = textX + metrics.horizontalAdvance(text.left(to));
                if (selected->lineEnd) {
                    right += metrics.horizontalAdvance(u' ');
                }
                const QRectF area(left, y, right - left, m_lineHeight);
                painter.fillRect(area, colors.color(QPalette::Highlight));
                painter.save();
                painter.setClipRect(area.intersected(QRectF(gutter, 0, width - gutter, height)));
                painter.setPen(colors.color(QPalette::HighlightedText));
                painter.drawText(textX, y + m_ascent, text);
                painter.restore();
            }
            painter.setClipping(false);
            widest = std::max(widest, static_cast<int>(std::ceil(metrics.horizontalAdvance(text))));

            if (gutter > 0) {
                const std::uint64_t lineNumber =
                    m_source->originalLineNumber(core::LineNumber(row)).get() + 1;
                painter.setPen(colors.color(QPalette::PlaceholderText));
                painter.drawText(QRect(markColumnWidth(), y,
                                       gutter - markColumnWidth() - kGutterPadding, m_lineHeight),
                                 Qt::AlignRight | Qt::AlignVCenter, QString::number(lineNumber));
            }
        }
        if (widest != m_maxTextWidth) {
            m_maxTextWidth = widest;
            updateScrollBars();
        }
    }
    m_lastPaintUs = timer.nsecsElapsed() / 1000;
}

}  // namespace lv::ui
