#include "ui/bookmarks_panel.h"

#include "model/bookmarks.h"
#include "model/line_source.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"

#include <QAction>
#include <QHeaderView>
#include <QKeySequence>
#include <QLocale>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lv::ui
{
namespace
{

enum Column : std::uint8_t
{
    kLineColumn = 0,
    kNameColumn = 1,
    kTextColumn = 2,
};

constexpr int kLineRole = Qt::UserRole;

}  // namespace

BookmarksPanel::BookmarksPanel(QWidget* parent)
    : QWidget(parent)
    , m_list(new QTreeWidget(this))
{
    m_list->setObjectName(QStringLiteral("bookmarksList"));
    m_list->setColumnCount(3);
    m_list->setHeaderLabels(
        {QStringLiteral("Line"), QStringLiteral("Name"), QStringLiteral("Text")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    // Double-click jumps; renaming is explicit (context menu), F2 belongs to "next bookmark".
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->header()->setStretchLastSection(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);

    auto* remove = new QAction(QStringLiteral("Delete"), this);
    remove->setShortcut(QKeySequence::Delete);
    remove->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(remove, &QAction::triggered, this, &BookmarksPanel::deleteSelected);
    addAction(remove);

    connect(m_list, &QTreeWidget::itemDoubleClicked, this,
            [this](const QTreeWidgetItem* item) { jumpTo(item); });
    connect(m_list, &QTreeWidget::itemActivated, this,
            [this](const QTreeWidgetItem* item) { jumpTo(item); });
    connect(m_list, &QTreeWidget::itemChanged, this, &BookmarksPanel::onItemChanged);
    connect(m_list, &QWidget::customContextMenuRequested, this, &BookmarksPanel::showContextMenu);
}

void BookmarksPanel::setTab(DocumentTab* tab)
{
    if (tab == m_tab) {
        return;
    }
    if (!m_tab.isNull()) {
        disconnect(&m_tab->bookmarks(), nullptr, this, nullptr);
    }
    m_tab = tab;
    if (tab != nullptr) {
        connect(&tab->bookmarks(), &model::Bookmarks::changed, this, &BookmarksPanel::rebuild);
    }
    rebuild();
}

void BookmarksPanel::rebuild()
{
    m_rebuilding = true;
    const QTreeWidgetItem* selected = m_list->currentItem();
    const auto selectedLine =
        selected != nullptr ? std::optional(selected->data(kLineColumn, kLineRole).toULongLong())
                            : std::nullopt;
    m_list->clear();
    if (!m_tab.isNull()) {
        const model::LineSource* source = m_tab->mainView()->lineSource();
        const QLocale locale;
        for (const auto& entry : m_tab->bookmarks().entries()) {
            QString text;
            if (source != nullptr && entry.line.get() < source->lineCount().get()) {
                const auto lines = source->plainLines(entry.line, core::LineCount(1));
                if (!lines.empty()) {
                    text = lines.front().text.left(kPreviewLength);
                }
            }
            auto* item = new QTreeWidgetItem(m_list);
            item->setText(kLineColumn, locale.toString(entry.line.get() + 1));
            item->setTextAlignment(kLineColumn, Qt::AlignRight | Qt::AlignVCenter);
            item->setData(kLineColumn, kLineRole, QVariant::fromValue(entry.line.get()));
            item->setText(kNameColumn, entry.name);
            item->setText(kTextColumn, text);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            if (selectedLine == entry.line.get()) {
                m_list->setCurrentItem(item);
            }
        }
    }
    m_list->resizeColumnToContents(kLineColumn);
    m_rebuilding = false;
}

void BookmarksPanel::jumpTo(const QTreeWidgetItem* item)
{
    if (item == nullptr || m_tab.isNull()) {
        return;
    }
    m_tab->goToLine(core::LineNumber(item->data(kLineColumn, kLineRole).toULongLong()));
}

void BookmarksPanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_rebuilding || column != kNameColumn || m_tab.isNull()) {
        return;
    }
    m_tab->bookmarks().rename(core::LineNumber(item->data(kLineColumn, kLineRole).toULongLong()),
                              item->text(kNameColumn));
}

void BookmarksPanel::renameSelected()
{
    if (QTreeWidgetItem* item = m_list->currentItem()) {
        m_list->editItem(item, kNameColumn);
    }
}

void BookmarksPanel::deleteSelected()
{
    const QTreeWidgetItem* item = m_list->currentItem();
    if (item == nullptr || m_tab.isNull()) {
        return;
    }
    m_tab->bookmarks().remove(core::LineNumber(item->data(kLineColumn, kLineRole).toULongLong()));
}

void BookmarksPanel::showContextMenu(const QPoint& position)
{
    const QTreeWidgetItem* item = m_list->itemAt(position);
    if (item == nullptr) {
        return;
    }
    QMenu menu(this);
    menu.addAction(QStringLiteral("Go to Line"), this, [this, item] { jumpTo(item); });
    menu.addAction(QStringLiteral("Rename"), this, &BookmarksPanel::renameSelected);
    menu.addAction(QStringLiteral("Delete"), this, &BookmarksPanel::deleteSelected);
    menu.exec(m_list->viewport()->mapToGlobal(position));
}

}  // namespace lv::ui
