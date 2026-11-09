#pragma once

#include <QFont>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <filesystem>

class QActionGroup;
class QLabel;
class QMenu;
class QUrl;
class QDockWidget;
class QTabWidget;

namespace lv::model
{
class FilterHistory;
class RecentFiles;
class Settings;
}  // namespace lv::model

namespace lv::ui
{

class BookmarksPanel;
class DocumentTab;

// The top-level window: tabs for documents, menus, and a status bar (SPEC F-FILE-1, F-TAB-1,
// F-TAB-2, F-TAB-3, F-TAB-4, F-TAB-6).
// Thread-safety: UI thread only.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    [[nodiscard]] QDockWidget* bookmarksDock() const noexcept { return m_bookmarksDock; }

    [[nodiscard]] BookmarksPanel* bookmarksPanel() const noexcept { return m_bookmarksPanel; }

    explicit MainWindow(QWidget* parent = nullptr);

    // The recent files list shown in File > Recent Files (F-FILE-9). By default the window owns
    // an in-memory list; the application installs one that is persisted in the settings.
    void setRecentFiles(model::RecentFiles* recent);

    // The global filter history (F-HIST-1); by default an in-memory one owned by the window.
    void setFilterHistory(model::FilterHistory* history);

    [[nodiscard]] model::FilterHistory* filterHistory() const noexcept { return m_filterHistory; }

    [[nodiscard]] model::RecentFiles* recentFiles() const noexcept { return m_recent; }

    // View settings (font, zoom, tab width, line numbers; F-VIEW-3, F-SET-1) applied to every
    // log view. Without settings, views use their defaults and zoom is kept in memory only.
    void setSettings(model::Settings* settings);

    [[nodiscard]] model::Settings* settings() const noexcept { return m_settings; }

    // Font used by log views: the settings' family (or the system fixed font) at the zoomed size.
    [[nodiscard]] QFont viewFont() const;

    // Rebuilds File > Recent Files (called before the menu is shown).
    void updateRecentMenu();

    [[nodiscard]] QMenu* recentMenu() const noexcept { return m_recentMenu; }

    [[nodiscard]] QMenu* encodingMenu() const noexcept { return m_encodingMenu; }

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
    void updateEncodingMenu();
    void applyViewSettings();
    void applyViewSettings(DocumentTab* tab);
    void changeZoom(int delta);

    QTabWidget* m_tabs = nullptr;
    QLabel* m_status = nullptr;
    model::RecentFiles* m_recent = nullptr;
    model::FilterHistory* m_filterHistory = nullptr;
    QMenu* m_recentMenu = nullptr;
    QMenu* m_encodingMenu = nullptr;
    QActionGroup* m_encodingGroup = nullptr;
    model::Settings* m_settings = nullptr;
    QAction* m_lineNumbersAction = nullptr;
    QAction* m_showMarksAction = nullptr;
    QAction* m_followAction = nullptr;
    QDockWidget* m_bookmarksDock = nullptr;
    BookmarksPanel* m_bookmarksPanel = nullptr;
    void updateFollowAction();
    void createBookmarkActions(QMenu* menu);
};

}  // namespace lv::ui
