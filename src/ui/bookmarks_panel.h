#pragma once

#include <QPointer>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace lv::ui
{

class DocumentTab;

// Lists the bookmarks of the current tab (SPEC F-BOOK-2): line number, name, and a preview of
// the line. Double-click (or Enter) jumps to the line; Rename in the context menu edits the name
// in place; Delete removes the bookmark.
//
// Thread-safety: UI thread only.
class BookmarksPanel : public QWidget
{
    Q_OBJECT

public:
    // Preview text is cut to this many characters.
    static constexpr qsizetype kPreviewLength = 200;

    explicit BookmarksPanel(QWidget* parent = nullptr);

    // Tab whose bookmarks are shown; null = none.
    void setTab(DocumentTab* tab);

    [[nodiscard]] QTreeWidget* list() const noexcept { return m_list; }

    // Starts editing the name of the selected bookmark.
    void renameSelected();
    void deleteSelected();

private:
    void rebuild();
    void jumpTo(const QTreeWidgetItem* item);
    void onItemChanged(QTreeWidgetItem* item, int column);
    void showContextMenu(const QPoint& position);

    QTreeWidget* m_list = nullptr;
    QPointer<DocumentTab> m_tab;
    bool m_rebuilding = false;
};

}  // namespace lv::ui
