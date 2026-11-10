#include "ui/overview_bar.h"

#include "model/bookmarks.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "model/mark_set.h"
#include "ui/log_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include <algorithm>

namespace lv::ui
{

OverviewBar::OverviewBar(LogView& view, QWidget* parent)
    : QWidget(parent)
    , m_view(view)
{
    setObjectName(QStringLiteral("overviewBar"));
    setFixedWidth(kWidth);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Overview: filter matches, marks, and bookmarks"));
    connect(&m_view, &LogView::topRowChanged, this, &OverviewBar::scheduleUpdate);
}

void OverviewBar::setSources(const model::FilteredDocument* filter, const model::MarkSet* marks,
                             const model::Bookmarks* bookmarks)
{
    m_filter = filter;
    m_marks = marks;
    m_bookmarks = bookmarks;
    if (filter != nullptr) {
        connect(filter, &model::FilteredDocument::matchesAppended, this,
                &OverviewBar::scheduleUpdate);
        connect(filter, &model::FilteredDocument::reset, this, &OverviewBar::scheduleUpdate);
        connect(filter, &model::FilteredDocument::stateChanged, this, &OverviewBar::scheduleUpdate);
        // The line count grows while the file is indexed or followed.
        connect(&filter->document(), &model::Document::linesAppended, this,
                &OverviewBar::scheduleUpdate);
        connect(&filter->document(), &model::Document::reset, this, &OverviewBar::scheduleUpdate);
    }
    if (marks != nullptr) {
        connect(marks, &model::MarkSet::changed, this, &OverviewBar::scheduleUpdate);
    }
    if (bookmarks != nullptr) {
        connect(bookmarks, &model::Bookmarks::changed, this, &OverviewBar::scheduleUpdate);
    }
    update();
}

void OverviewBar::scheduleUpdate()
{
    // Coalesce bursts (matches arrive in many small batches while searching).
    if (m_updatePending) {
        return;
    }
    m_updatePending = true;
    QTimer::singleShot(50, this, [this] {
        m_updatePending = false;
        update();
    });
}

std::uint64_t OverviewBar::lineCount() const
{
    return m_view.rowCount().get();
}

core::LineNumber OverviewBar::lineAt(int y) const
{
    const std::uint64_t lines = lineCount();
    if (lines == 0 || height() <= 0) {
        return core::LineNumber(0);
    }
    const auto row = static_cast<double>(std::clamp(y, 0, height() - 1));
    const auto line = static_cast<std::uint64_t>(row / static_cast<double>(height()) *
                                                 static_cast<double>(lines));
    return core::LineNumber(std::min(line, lines - 1));
}

int OverviewBar::yOf(core::LineNumber line) const
{
    const std::uint64_t lines = lineCount();
    if (lines == 0) {
        return 0;
    }
    const double fraction = static_cast<double>(line.get()) / static_cast<double>(lines);
    return std::clamp(static_cast<int>(fraction * height()), 0, std::max(0, height() - 1));
}

bool OverviewBar::hasMatchAt(int y) const
{
    if (m_filter == nullptr || m_filter->matchCount() == 0 || lineCount() == 0) {
        return false;
    }
    const core::LineNumber first = lineAt(y);
    const std::uint64_t end =
        y + 1 >= height() ? lineCount() : std::max(first.get() + 1, lineAt(y + 1).get());
    const std::uint64_t index = m_filter->lowerBound(first);
    return index < m_filter->matchCount() && m_filter->matchAt(index).get() < end;
}

QSize OverviewBar::sizeHint() const
{
    return {kWidth, 100};
}

void OverviewBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    if (lineCount() == 0) {
        return;
    }
    // Visible range.
    QColor visible = palette().color(QPalette::Text);
    visible.setAlpha(40);
    const int top = yOf(m_view.topRow());
    const int bottom = yOf(core::LineNumber(m_view.topRow().get() + m_view.pageRows()));
    painter.fillRect(QRect(0, top, width(), std::max(2, bottom - top)), visible);

    // Filter matches (left half).
    QColor match = m_view.matchHighlightColor();
    match.setAlpha(255);
    for (int y = 0; y < height(); ++y) {
        if (hasMatchAt(y)) {
            painter.fillRect(QRect(1, y, width() / 2, 1), match);
        }
    }
    // Marks (right half) in their colors.
    if (m_marks != nullptr) {
        for (const core::LineNumber line : m_marks->lines()) {
            QColor color = m_view.markColor(m_marks->colorOf(line).value_or(0));
            color.setAlpha(255);
            painter.fillRect(QRect(width() / 2, yOf(line), (width() / 2) - 1, 2), color);
        }
    }
    // Bookmarks: full width, on top.
    if (m_bookmarks != nullptr) {
        for (const auto& entry : m_bookmarks->entries()) {
            painter.fillRect(QRect(0, yOf(entry.line), width(), 3), m_view.bookmarkColor());
        }
    }
}

void OverviewBar::jumpTo(int y)
{
    if (lineCount() == 0) {
        return;
    }
    m_view.goToRow(lineAt(y));
}

void OverviewBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        jumpTo(static_cast<int>(event->position().y()));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void OverviewBar::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) != 0) {
        jumpTo(static_cast<int>(event->position().y()));  // dragging scrubs through the file
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

}  // namespace lv::ui
