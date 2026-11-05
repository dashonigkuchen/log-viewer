#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <filesystem>

class QLabel;
class QUrl;
class QTabWidget;

namespace lv::ui
{

class DocumentTab;

// The top-level window: tabs for documents, menus, and a status bar (SPEC F-FILE-1, F-TAB-1,
// F-TAB-2, F-TAB-3, F-TAB-4, F-TAB-6).
// Thread-safety: UI thread only.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    [[nodiscard]] QTabWidget* tabWidget() const noexcept { return m_tabs; }

    // Opens `path` in a new tab, or activates its tab if it is already open. Used by the Open
    // dialog, drag and drop, and the command line. Returns the tab.
    DocumentTab* openFile(const std::filesystem::path& path);
    void openFiles(const QStringList& paths);

    // Opens the local files among dropped URLs (drag and drop, F-FILE-1).
    void openDroppedUrls(const QList<QUrl>& urls);

    [[nodiscard]] DocumentTab* currentDocumentTab() const;
    [[nodiscard]] DocumentTab* documentTab(int index) const;

    void closeTab(int index);
    void closeOtherTabs(int index);
    void closeAllTabs();

    // Shows the open file dialog (Ctrl+O).
    void showOpenDialog();

    // Text of the status bar's document part (F-TAB-6).
    [[nodiscard]] QString statusText() const;

    // Rich text shown in the About dialog: version, license, Qt version, AI note.
    [[nodiscard]] static QString aboutText();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void createMenus();
    void showAbout();
    void showTabContextMenu(const QPoint& position);
    void updateTabState(DocumentTab* tab);
    void updateStatus();

    QTabWidget* m_tabs = nullptr;
    QLabel* m_status = nullptr;
};

}  // namespace lv::ui
