#include "ui/log_view.h"

#include "core/encoding/decode.h"
#include "core/util/assert.h"
#include "model/line_source.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>

#include <algorithm>
#include <cmath>
#include <string_view>

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

int LogView::gutterWidth() const
{
    if (!m_lineNumbersVisible) {
        return 0;
    }
    const QFontMetricsF metrics(font());
    const int digits = digitCount(std::max<std::uint64_t>(rowCount().get(), 1));
    return static_cast<int>(std::ceil(metrics.horizontalAdvance(QString(digits, u'9')))) +
           (2 * kGutterPadding);
}

QString LogView::displayText(const QString& text, bool truncatedByReader) const
{
    const auto limit = static_cast<std::size_t>(m_columnLimit);
    // Cut before expanding tabs: a line can only grow by expansion.
    const bool cut = static_cast<std::size_t>(text.size()) > limit;
    const std::u16string_view raw(reinterpret_cast<const char16_t*>(text.utf16()),
                                  cut ? limit : static_cast<std::size_t>(text.size()));
    std::u16string expanded = core::expandTabs(raw, static_cast<std::uint32_t>(m_tabWidth));
    if (expanded.size() > limit) {
        expanded.resize(limit);
    }
    if (cut || truncatedByReader) {
        expanded.push_back(kTruncationMarker);
    }
    return QString::fromUtf16(expanded.data(), static_cast<qsizetype>(expanded.size()));
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
}

void LogView::onReset()
{
    m_topRow = core::LineNumber(0);
    m_currentRow.reset();
    m_maxTextWidth = 0;
    if (rowCount().get() > 0) {
        m_currentRow = core::LineNumber(0);
    }
    updateScrollBars();
    viewport()->update();
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
            if (m_currentRow && m_currentRow->get() == row) {
                painter.fillRect(gutter, y, width - gutter, m_lineHeight,
                                 colors.color(QPalette::Highlight).lighter(170));
            }
            const auto& line = lines[i];
            const QString text = displayText(line.text, line.truncated);
            painter.setPen(
                colors.color(line.unavailable ? QPalette::PlaceholderText : QPalette::Text));
            painter.setClipRect(gutter, 0, width - gutter, height);
            painter.drawText(textX, y + m_ascent, text);
            painter.setClipping(false);
            widest = std::max(widest, static_cast<int>(std::ceil(metrics.horizontalAdvance(text))));

            if (gutter > 0) {
                const std::uint64_t lineNumber =
                    m_source->originalLineNumber(core::LineNumber(row)).get() + 1;
                painter.setPen(colors.color(QPalette::PlaceholderText));
                painter.drawText(QRect(0, y, gutter - kGutterPadding, m_lineHeight),
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
