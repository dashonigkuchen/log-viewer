#pragma once

#include <QFont>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <filesystem>
#include <functional>

class QActionGroup;
class QLabel;
class QMenu;
class QUrl;
class QDockWidget;
class QTabWidget;

namespace lv::model
{
class AnnotationStore;
struct Session;
class FilterHistory;
class HighlightSets;
class RecentFiles;
class Settings;
enum class ColorScheme : std::uint8_t;
}  // namespace lv::model

namespace lv::ui
{
struct OpenRequest;

class BookmarksPanel;
class HighlightDialog;
class SettingsDialog;
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
    // Persists marks and bookmarks per file (F-PERS-1); null = not persisted. Not owned.
    void setAnnotationStore(model::AnnotationStore* store);
    // Highlight sets for all tabs (F-HL-3); the window creates its own until one is set.
    void setHighlightSets(model::HighlightSets* sets);

    [[nodiscard]] model::HighlightSets* highlightSets() const noexcept { return m_highlightSets; }

    // Opens the highlight rules dialog (F-HL-4); `prepare` may adjust it before it is shown
    // (tests). Applies the result on OK.
    void editHighlightRules(const std::function<void(HighlightDialog&)>& prepare = {});
    // Opens the settings dialog (F-SET-1); `prepare` may adjust it before it is shown (tests).
    void editSettings(const std::function<void(SettingsDialog&)>& prepare = {});
    // Qt's scheme for a setting; Unknown = follow the system (F-SET-4).
    [[nodiscard]] static Qt::ColorScheme toQtColorScheme(model::ColorScheme scheme);
    // Adds a match-only rule for `text` to the active set of `tab` (F-HL-7).
    void highlightText(DocumentTab* tab, const QString& text);

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

    // Opens the files of a request (F-CLI-2): follow mode, and a jump to `line` (1-based) in
    // the first file once it is loaded. Brings the window to the front.
    void openRequest(const OpenRequest& request);

    // Session restore (F-FILE-10, F-TAB-5): the open tabs and the window layout.
    [[nodiscard]] model::Session session() const;
    // Restores the window layout and reopens the tabs; missing files are skipped and reported
    // in the status bar. Returns the number of tabs opened.
    int restoreSession(const model::Session& session, bool reopenTabs = true);

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
    // Saves the session to the settings (if set) before closing.
    void closeEvent(QCloseEvent* event) override;

private:
    void createMenus();
    void showAbout();
    void showTabContextMenu(const QPoint& position);
    void updateTabState(DocumentTab* tab);
    void updateStatus();
    void updateEncodingMenu();
    void applyViewSettings();
    void applyViewSettings(DocumentTab* tab);
    static void applyColorScheme(model::ColorScheme scheme);
    void changeZoom(int delta);

    QTabWidget* m_tabs = nullptr;
    QLabel* m_status = nullptr;
    model::RecentFiles* m_recent = nullptr;
    model::FilterHistory* m_filterHistory = nullptr;
    model::HighlightSets* m_highlightSets = nullptr;
    model::AnnotationStore* m_annotationStore = nullptr;
    QMenu* m_highlightSetMenu = nullptr;
    void updateHighlightSetMenu();
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
