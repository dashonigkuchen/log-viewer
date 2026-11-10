#pragma once

#include "core/util/strong_types.h"

#include <QWidget>

#include <cstdint>

namespace lv::model
{
class Bookmarks;
class FilteredDocument;
class MarkSet;
}  // namespace lv::model

namespace lv::ui
{

class LogView;

// Overview bar next to the vertical scroll bar of the main view (SPEC F-VIEW-11): ticks for
// filter matches, marks, and bookmarks over the whole file, and the visible range. Clicking
// jumps there. Matches are sampled per pixel row, so painting is O(height * log matches).
//
// Thread-safety: UI thread only.
class OverviewBar : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kWidth = 14;

    explicit OverviewBar(LogView& view, QWidget* parent = nullptr);

    // Any source may be null.
    void setSources(const model::FilteredDocument* filter, const model::MarkSet* marks,
                    const model::Bookmarks* bookmarks);

    // The line at pixel row `y` (for clicks and tests).
    [[nodiscard]] core::LineNumber lineAt(int y) const;
    // The first pixel row of `line`.
    [[nodiscard]] int yOf(core::LineNumber line) const;
    // True if pixel row `y` shows at least one filter match.
    [[nodiscard]] bool hasMatchAt(int y) const;

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] std::uint64_t lineCount() const;
    void jumpTo(int y);
    void scheduleUpdate();

    LogView& m_view;
    const model::FilteredDocument* m_filter = nullptr;
    const model::MarkSet* m_marks = nullptr;
    const model::Bookmarks* m_bookmarks = nullptr;
    bool m_updatePending = false;
};

}  // namespace lv::ui
