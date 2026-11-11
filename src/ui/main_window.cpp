#include "ui/main_window.h"

#include "model/document.h"
#include "model/filter_history.h"
#include "model/highlight_sets.h"
#include "model/line_highlighter.h"
#include "model/model_info.h"
#include "model/recent_files.h"
#include "model/session.h"
#include "model/settings.h"
#include "ui/bookmarks_panel.h"
#include "ui/document_tab.h"
#include "ui/filter_bar.h"
#include "ui/find_bar.h"
#include "ui/highlight_dialog.h"
#include "ui/log_view.h"
#include "ui/settings_dialog.h"
#include "ui/shortcuts.h"
#include "ui/single_instance.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QProcess>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QtVersion>

namespace lv::ui
{

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_tabs(new QTabWidget(this))
    , m_status(new QLabel(this))
    , m_recent(new model::RecentFiles(this))
    , m_filterHistory(new model::FilterHistory(this))
    , m_highlightSets(new model::HighlightSets(this))
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Log Viewer %1").arg(lv::model::versionString()));
    resize(1024, 768);
    setAcceptDrops(true);

    m_tabs->setObjectName(QStringLiteral("documentTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updateStatus);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updateEncodingMenu);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updateFollowAction);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        // The current tab no longer needs the "changed" indicator (F-TAB-3).
        if (auto* tab = currentDocumentTab()) {
            tab->setProperty("lvChanged", false);
            updateTabState(tab);
        }
    });
    connect(m_tabs->tabBar(), &QTabBar::customContextMenuRequested, this,
            &MainWindow::showTabContextMenu);
    // Middle click closes a tab (F-TAB-2).
    m_tabs->tabBar()->installEventFilter(this);

    m_status->setObjectName(QStringLiteral("documentStatus"));
    statusBar()->addWidget(m_status, 1);
    createMenus();
    updateStatus();
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->setObjectName(QStringLiteral("menuFile"));

    QAction* openAction = fileMenu->addAction(QStringLiteral("&Open..."));
    openAction->setObjectName(QStringLiteral("actionOpen"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::showOpenDialog);

    m_recentMenu = fileMenu->addMenu(QStringLiteral("Recent &Files"));
    m_recentMenu->setObjectName(QStringLiteral("menuRecentFiles"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentMenu);

    QAction* reloadAction = fileMenu->addAction(QStringLiteral("&Reload"));
    reloadAction->setObjectName(QStringLiteral("actionReload"));
    reloadAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(reloadAction, &QAction::triggered, this, [this] {
        if (auto* tab = currentDocumentTab()) {
            tab->reload();
        }
    });

    QAction* closeAction = fileMenu->addAction(QStringLiteral("&Close"));
    closeAction->setObjectName(QStringLiteral("actionClose"));
    closeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    connect(closeAction, &QAction::triggered, this, [this] { closeTab(m_tabs->currentIndex()); });

    QAction* closeAllAction = fileMenu->addAction(QStringLiteral("Close &All"));
    closeAllAction->setObjectName(QStringLiteral("actionCloseAll"));
    connect(closeAllAction, &QAction::triggered, this, &MainWindow::closeAllTabs);

    fileMenu->addSeparator();
    QAction* settingsAction = fileMenu->addAction(QStringLiteral("&Settings..."));
    settingsAction->setObjectName(QStringLiteral("actionSettings"));
    settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(settingsAction, &QAction::triggered, this, [this] { editSettings(); });
    QAction* shortcutsAction = fileMenu->addAction(QStringLiteral("&Keyboard Shortcuts..."));
    shortcutsAction->setObjectName(QStringLiteral("actionKeyboardShortcuts"));
    connect(shortcutsAction, &QAction::triggered, this, [this] { editShortcuts(); });
    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction(QStringLiteral("E&xit"));
    exitAction->setObjectName(QStringLiteral("actionExit"));
    exitAction->setShortcut(QKeySequence::Quit);
    exitAction->setMenuRole(QAction::QuitRole);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    // Ctrl+Tab / Ctrl+Shift+Tab switch tabs (F-TAB-2).
    auto* nextTab = new QAction(QStringLiteral("Next Tab"), this);
    nextTab->setObjectName(QStringLiteral("actionNextTab"));
    nextTab->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence::NextChild});
    connect(nextTab, &QAction::triggered, this, [this] {
        if (m_tabs->count() > 1) {
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
        }
    });
    addAction(nextTab);
    auto* previousTab = new QAction(QStringLiteral("Previous Tab"), this);
    previousTab->setObjectName(QStringLiteral("actionPreviousTab"));
    previousTab->setShortcuts(
        {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab), QKeySequence::PreviousChild});
    connect(previousTab, &QAction::triggered, this, [this] {
        if (m_tabs->count() > 1) {
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) %
                                    m_tabs->count());
        }
    });
    addAction(previousTab);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->setObjectName(QStringLiteral("menuView"));
    QAction* zoomIn = viewMenu->addAction(QStringLiteral("Zoom &In"));
    zoomIn->setObjectName(QStringLiteral("actionZoomIn"));
    zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    connect(zoomIn, &QAction::triggered, this, [this] { changeZoom(1); });
    QAction* zoomOut = viewMenu->addAction(QStringLiteral("Zoom &Out"));
    zoomOut->setObjectName(QStringLiteral("actionZoomOut"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this, [this] { changeZoom(-1); });
    QAction* zoomReset = viewMenu->addAction(QStringLiteral("&Reset Zoom"));
    zoomReset->setObjectName(QStringLiteral("actionZoomReset"));
    zoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(zoomReset, &QAction::triggered, this, [this] {
        if (m_settings != nullptr) {
            m_settings->setZoom(0);
        }
    });
    m_lineNumbersAction = viewMenu->addAction(QStringLiteral("&Line Numbers"));
    m_lineNumbersAction->setObjectName(QStringLiteral("actionLineNumbers"));
    m_lineNumbersAction->setCheckable(true);
    m_lineNumbersAction->setChecked(true);
    connect(m_lineNumbersAction, &QAction::toggled, this, [this](bool visible) {
        if (m_settings != nullptr) {
            m_settings->setLineNumbersVisible(visible);
        } else {
            applyViewSettings();
        }
    });
    m_showMarksAction = viewMenu->addAction(QStringLiteral("Show &Marks in Filtered View"));
    m_showMarksAction->setObjectName(QStringLiteral("actionShowMarksInFilter"));
    m_showMarksAction->setCheckable(true);
    m_showMarksAction->setChecked(true);
    connect(m_showMarksAction, &QAction::toggled, this, [this](bool show) {
        if (m_settings != nullptr) {
            m_settings->setShowMarksInFilter(show);
        } else {
            applyViewSettings();
        }
    });
    viewMenu->addSeparator();

    // Focus the filter field of the current tab (Ctrl+Shift+F; Ctrl+F is the find bar, F-FIND-1).
    QAction* focusFilter = viewMenu->addAction(QStringLiteral("&Filter..."));
    focusFilter->setObjectName(QStringLiteral("actionFocusFilter"));
    focusFilter->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(focusFilter, &QAction::triggered, this, [this] {
        if (auto* tab = currentDocumentTab()) {
            tab->filterBar()->input()->setFocus(Qt::ShortcutFocusReason);
            tab->filterBar()->input()->selectAll();
        }
    });
    QAction* find = viewMenu->addAction(QStringLiteral("F&ind..."));
    find->setObjectName(QStringLiteral("actionFind"));
    find->setShortcut(QKeySequence::Find);
    connect(find, &QAction::triggered, this, [this] {
        if (auto* tab = currentDocumentTab()) {
            tab->openFindBar();
        }
    });
    QAction* findNext = viewMenu->addAction(QStringLiteral("Find &Next"));
    findNext->setObjectName(QStringLiteral("actionFindNext"));
    findNext->setShortcut(QKeySequence(Qt::Key_F3));
    connect(findNext, &QAction::triggered, this, [this] {
        if (auto* tab = currentDocumentTab()) {
            tab->findBar()->findNext();
        }
    });
    QAction* findPrevious = viewMenu->addAction(QStringLiteral("Find &Previous"));
    findPrevious->setObjectName(QStringLiteral("actionFindPrevious"));
    findPrevious->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    connect(findPrevious, &QAction::triggered, this, [this] {
        if (auto* tab = currentDocumentTab()) {
            tab->findBar()->findPrevious();
        }
    });
    // Follow mode of the current tab (F-TAIL-3).
    m_followAction = viewMenu->addAction(QStringLiteral("F&ollow"));
    m_followAction->setObjectName(QStringLiteral("actionFollow"));
    m_followAction->setCheckable(true);
    m_followAction->setShortcut(QKeySequence(Qt::Key_F10));
    m_followAction->setToolTip(QStringLiteral("Follow the end of the file (F10)"));
    connect(m_followAction, &QAction::triggered, this, [this](bool on) {
        if (auto* tab = currentDocumentTab()) {
            tab->setFollowing(on);
        } else {
            m_followAction->setChecked(false);
        }
    });
    createBookmarkActions(viewMenu);
    // Highlight set of the current tab (F-HL-3).
    m_highlightSetMenu = viewMenu->addMenu(QStringLiteral("&Highlight Set"));
    m_highlightSetMenu->setObjectName(QStringLiteral("menuHighlightSet"));
    connect(m_highlightSetMenu, &QMenu::aboutToShow, this, &MainWindow::updateHighlightSetMenu);
    updateHighlightSetMenu();
    QAction* editRules = viewMenu->addAction(QStringLiteral("Edit Highlight &Rules..."));
    editRules->setObjectName(QStringLiteral("actionEditHighlightRules"));
    connect(editRules, &QAction::triggered, this, [this] { editHighlightRules(); });
    auto* toolBar = addToolBar(QStringLiteral("Main"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->addAction(m_followAction);
    QAction* clearHistory = viewMenu->addAction(QStringLiteral("Clear Filter &History"));
    clearHistory->setObjectName(QStringLiteral("actionClearFilterHistory"));
    connect(clearHistory, &QAction::triggered, this, [this] {
        if (m_filterHistory != nullptr) {
            m_filterHistory->clear();
        }
    });
    viewMenu->addSeparator();

    m_encodingMenu = viewMenu->addMenu(QStringLiteral("&Encoding"));
    m_encodingMenu->setObjectName(QStringLiteral("menuEncoding"));
    m_encodingGroup = new QActionGroup(this);
    m_encodingGroup->setExclusive(true);
    auto addEncoding = [this](const QString& text, std::optional<core::Encoding> encoding) {
        QAction* action = m_encodingMenu->addAction(text);
        action->setCheckable(true);
        action->setData(encoding ? QVariant(static_cast<int>(*encoding)) : QVariant());
        m_encodingGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, encoding] {
            if (auto* tab = currentDocumentTab()) {
                tab->setEncodingOverride(encoding);
            }
            updateEncodingMenu();
        });
    };
    addEncoding(QStringLiteral("&Auto-detect"), std::nullopt);
    m_encodingMenu->addSeparator();
    for (const auto encoding :
         {core::Encoding::kUtf8, core::Encoding::kUtf16Le, core::Encoding::kUtf16Be,
          core::Encoding::kWindows1252, core::Encoding::kIso88591, core::Encoding::kWindows1250,
          core::Encoding::kWindows1251, core::Encoding::kIso885915}) {
        addEncoding(QString::fromUtf8(core::displayName(encoding).data()), encoding);
    }
    updateEncodingMenu();

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->setObjectName(QStringLiteral("menuHelp"));

    QAction* aboutAction = helpMenu->addAction(QStringLiteral("&About Log Viewer"));
    aboutAction->setObjectName(QStringLiteral("actionAbout"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    collectShortcutActions();
}

void MainWindow::collectShortcutActions()
{
    // Menu actions and window actions with a stable object name; the generated entries of the
    // recent files, encoding, and highlight set menus are not configurable.
    for (QAction* action : findChildren<QAction*>()) {
        QObject* owner = action->parent();
        const bool generated =
            owner == m_recentMenu || owner == m_encodingMenu || owner == m_highlightSetMenu;
        const bool ours =
            owner == this || qobject_cast<QMenu*>(owner) != nullptr || owner == m_bookmarksDock;
        if (!generated && ours && action->objectName().startsWith(QStringLiteral("action")) &&
            !action->text().isEmpty() && action->menu() == nullptr) {
            rememberDefaultShortcuts(*action);
            m_shortcutActions.push_back(action);
        }
    }
}

void MainWindow::editShortcuts(const std::function<void(ShortcutsDialog&)>& prepare)
{
    if (m_settings == nullptr) {
        return;
    }
    ShortcutsDialog dialog(m_shortcutActions, m_settings->shortcuts(), this);
    if (prepare) {
        prepare(dialog);
    }
    if (dialog.exec() == QDialog::Accepted) {
        m_settings->setShortcuts(dialog.overrides());
        m_settings->sync();
    }
}

void MainWindow::setRecentFiles(model::RecentFiles* recent)
{
    m_recent = recent;
}

void MainWindow::setSettings(model::Settings* settings)
{
    if (m_settings != nullptr) {
        disconnect(m_settings, nullptr, this, nullptr);
    }
    m_settings = settings;
    if (m_settings != nullptr) {
        connect(m_settings, &model::Settings::viewSettingsChanged, this,
                qOverload<>(&MainWindow::applyViewSettings));
        connect(m_settings, &model::Settings::settingsChanged, this,
                qOverload<>(&MainWindow::applyViewSettings));
        if (!m_settings->corruptBackup().isEmpty()) {
            statusBar()->showMessage(
                QStringLiteral("The settings file was damaged and has been reset (backup: %1).")
                    .arg(QDir::toNativeSeparators(m_settings->corruptBackup())),
                15000);
        }
        m_lineNumbersAction->setChecked(m_settings->lineNumbersVisible());
        m_showMarksAction->setChecked(m_settings->showMarksInFilter());
    }
    applyViewSettings();
}

QFont MainWindow::viewFont() const
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (m_settings != nullptr) {
        if (!m_settings->fontFamily().isEmpty()) {
            font.setFamily(m_settings->fontFamily());
        }
        font.setPointSize(m_settings->effectiveFontSize());
    }
    return font;
}

void MainWindow::changeZoom(int delta)
{
    if (m_settings != nullptr) {
        m_settings->setZoom(m_settings->zoom() + delta);
    }
}

void MainWindow::applyViewSettings()
{
    if (m_settings != nullptr) {
        applyColorScheme(m_settings->colorScheme());
        applyShortcuts(m_shortcutActions, m_settings->shortcuts());
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        applyViewSettings(documentTab(i));
    }
}

void MainWindow::applyColorScheme(model::ColorScheme scheme)
{
    // F-SET-4: Qt follows the system theme unless a scheme is requested.
    // Platforms without theme support (for example offscreen) ignore the request.
    QGuiApplication::styleHints()->setColorScheme(toQtColorScheme(scheme));
}

Qt::ColorScheme MainWindow::toQtColorScheme(model::ColorScheme scheme)
{
    switch (scheme) {
    case model::ColorScheme::kLight:
        return Qt::ColorScheme::Light;
    case model::ColorScheme::kDark:
        return Qt::ColorScheme::Dark;
    case model::ColorScheme::kSystem:
        break;
    }
    return Qt::ColorScheme::Unknown;
}

void MainWindow::applyViewSettings(DocumentTab* tab)
{
    if (tab == nullptr) {
        return;
    }
    tab->setShowMarksInFilter(m_settings != nullptr ? m_settings->showMarksInFilter()
                                                    : m_showMarksAction->isChecked());
    if (m_settings != nullptr) {
        tab->document().setPollInterval(m_settings->pollInterval());
        if (m_filterHistory != nullptr) {
            m_filterHistory->setLimit(m_settings->filterHistorySize());
        }
    }
    for (LogView* view : {tab->mainView(), tab->filteredView()}) {
        if (m_settings != nullptr) {
            view->setFont(viewFont());
            view->setTabWidth(m_settings->tabWidth());
            view->setLineNumbersVisible(m_settings->lineNumbersVisible());
            view->setColumnLimit(m_settings->maxColumns());
            const auto toColor = [](std::optional<std::uint32_t> rgba) {
                return rgba.has_value() ? QColor::fromRgba(*rgba) : QColor();
            };
            view->setMarkColor(toColor(m_settings->markColor()));
            view->setMatchHighlightColor(toColor(m_settings->matchColor()));
            const std::optional<std::uint32_t> currentLine = m_settings->currentLineColor();
            view->setCurrentLineColor(currentLine.has_value() ? QColor::fromRgba(*currentLine)
                                                              : QColor());
        } else {
            view->setLineNumbersVisible(m_lineNumbersAction->isChecked());
        }
    }
}

void MainWindow::setFilterHistory(model::FilterHistory* history)
{
    m_filterHistory = history;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* tab = documentTab(i)) {
            tab->filterBar()->setHistory(history);
        }
    }
}

void MainWindow::setAnnotationStore(model::AnnotationStore* store)
{
    m_annotationStore = store;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* tab = documentTab(i)) {
            tab->setAnnotationStore(store);
        }
    }
}

void MainWindow::setHighlightSets(model::HighlightSets* sets)
{
    m_highlightSets = sets;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* tab = documentTab(i)) {
            tab->lineHighlighter().setSets(sets);
        }
    }
    updateHighlightSetMenu();
}

void MainWindow::editHighlightRules(const std::function<void(HighlightDialog&)>& prepare)
{
    // Preview: the lines on screen of the current tab, otherwise sample lines.
    QStringList preview;
    if (const auto* tab = currentDocumentTab()) {
        const LogView* view = tab->mainView();
        if (const auto* source = view->lineSource(); source != nullptr) {
            const std::uint64_t rows = source->lineCount().get();
            const std::uint64_t first = std::min(view->topRow().get(), rows);
            const std::uint64_t count = std::min<std::uint64_t>(rows - first, 30);
            for (const auto& line :
                 source->plainLines(core::LineNumber(first), core::LineCount(count))) {
                preview << line.text;
            }
        }
    }
    HighlightDialog dialog(*m_highlightSets, preview, this);
    if (const auto* tab = currentDocumentTab()) {
        if (const auto active = tab->lineHighlighter().activeSet()) {
            dialog.selectSet(*active);
        }
    }
    if (prepare) {
        prepare(dialog);
    }
    if (dialog.exec() == QDialog::Accepted) {
        m_highlightSets->assign(dialog.sets(), dialog.defaultIndex());
    }
}

void MainWindow::editSettings(const std::function<void(SettingsDialog&)>& prepare)
{
    if (m_settings == nullptr) {
        return;
    }
    SettingsDialog dialog(*m_settings, m_filterHistory, this);
    if (prepare) {
        prepare(dialog);
    }
    dialog.exec();
}

void MainWindow::highlightText(DocumentTab* tab, const QString& text)
{
    if (tab == nullptr || text.isEmpty()) {
        return;
    }
    const auto active = tab->lineHighlighter().activeSet();
    if (!active) {
        return;
    }
    model::HighlightSet set = m_highlightSets->set(*active);
    set.rules.push_back({.pattern = text,
                         .regex = false,
                         .caseSensitive = true,
                         .scope = model::HighlightScope::kMatchOnly,
                         .foreground = std::nullopt,
                         .background = 0xFF80DEEAU,  // light cyan, distinct from the filter color
                         .enabled = true});
    m_highlightSets->replace(*active, std::move(set));
}

void MainWindow::updateHighlightSetMenu()
{
    m_highlightSetMenu->clear();
    auto* group = new QActionGroup(m_highlightSetMenu);
    const auto* tab = currentDocumentTab();
    const auto chosen = tab != nullptr ? tab->lineHighlighter().chosenSet() : std::nullopt;
    const auto addEntry = [&](const QString& text, std::optional<std::size_t> index) {
        QAction* action = m_highlightSetMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(index == chosen);
        action->setEnabled(tab != nullptr);
        action->setActionGroup(group);
        connect(action, &QAction::triggered, this, [this, index] {
            if (auto* current = currentDocumentTab()) {
                current->lineHighlighter().setActiveSet(index);
            }
        });
    };
    QString defaultName;
    if (m_highlightSets != nullptr && m_highlightSets->size() > 0) {
        defaultName = m_highlightSets->set(m_highlightSets->defaultIndex()).name;
    }
    addEntry(QStringLiteral("Default (%1)").arg(defaultName), std::nullopt);
    m_highlightSetMenu->addSeparator();
    if (m_highlightSets != nullptr) {
        for (std::size_t i = 0; i < m_highlightSets->size(); ++i) {
            addEntry(m_highlightSets->set(i).name, i);
        }
    }
    QAction* none = m_highlightSetMenu->actions().value(0);
    if (none != nullptr) {
        none->setObjectName(QStringLiteral("actionHighlightSetDefault"));
    }
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    if (m_recent == nullptr || m_recent->paths().isEmpty()) {
        m_recentMenu->addAction(QStringLiteral("(empty)"))->setEnabled(false);
        return;
    }
    int number = 1;
    for (const QString& path : m_recent->paths()) {
        const QString label = number <= 9 ? QStringLiteral("&%1 %2").arg(number).arg(path)
                                          : QStringLiteral("%1 %2").arg(number).arg(path);
        QAction* action = m_recentMenu->addAction(QDir::toNativeSeparators(label));
        action->setData(path);
        action->setEnabled(QFileInfo::exists(path));  // missing files are shown disabled
        connect(action, &QAction::triggered, this, [this, path] { openFiles({path}); });
        ++number;
    }
    m_recentMenu->addSeparator();
    QAction* removeMissing = m_recentMenu->addAction(QStringLiteral("Remove Missing Files"));
    removeMissing->setObjectName(QStringLiteral("actionRemoveMissingRecent"));
    connect(removeMissing, &QAction::triggered, m_recent, &model::RecentFiles::removeMissing);
    QAction* clear = m_recentMenu->addAction(QStringLiteral("Clear List"));
    clear->setObjectName(QStringLiteral("actionClearRecent"));
    connect(clear, &QAction::triggered, m_recent, &model::RecentFiles::clear);
}

void MainWindow::updateEncodingMenu()
{
    const auto* tab = currentDocumentTab();
    m_encodingMenu->setEnabled(tab != nullptr);
    if (tab == nullptr) {
        return;
    }
    const auto current = tab->document().encodingOverride();
    for (QAction* action : m_encodingGroup->actions()) {
        const QVariant value = action->data();
        bool matches = !value.isValid();
        if (current) {
            matches = value.isValid() && value.toInt() == static_cast<int>(*current);
        }
        action->setChecked(matches);
    }
}

DocumentTab* MainWindow::documentTab(int index) const
{
    return qobject_cast<DocumentTab*>(m_tabs->widget(index));
}

DocumentTab* MainWindow::currentDocumentTab() const
{
    return documentTab(m_tabs->currentIndex());
}

DocumentTab* MainWindow::openFile(const std::filesystem::path& path)
{
    const auto normalized = normalizedPath(path);
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* tab = documentTab(i); tab != nullptr && tab->canonicalPath() == normalized) {
            m_tabs->setCurrentIndex(i);
            return tab;
        }
    }
    if (m_recent != nullptr) {
        m_recent->add(QString::fromStdString(normalized.string()));
    }
    auto* tab = new DocumentTab(normalized, m_tabs);
    const int index = m_tabs->addTab(tab, tab->title());
    m_tabs->setTabToolTip(index, tab->toolTip());
    connect(tab, &DocumentTab::statusChanged, this, [this, tab] {
        updateTabState(tab);
        if (tab == currentDocumentTab()) {
            updateStatus();
        }
    });
    applyViewSettings(tab);
    tab->filterBar()->setHistory(m_filterHistory);
    tab->lineHighlighter().setSets(m_highlightSets);
    tab->setAnnotationStore(m_annotationStore);
    connect(tab, &DocumentTab::highlightTextRequested, this,
            [this, tab](const QString& text) { highlightText(tab, text); });
    connect(tab, &DocumentTab::followingChanged, this, [this, tab] {
        if (tab == currentDocumentTab()) {
            updateFollowAction();
        }
    });
    connect(tab, &DocumentTab::contentChanged, this, [this, tab] {
        if (tab != currentDocumentTab()) {
            tab->setProperty("lvChanged", true);
            updateTabState(tab);
        }
    });
    if (m_settings != nullptr) {
        const QByteArray state = m_settings->filterSplitterState();
        if (!state.isEmpty()) {
            tab->splitter()->restoreState(state);
        }
    }
    connect(tab, &DocumentTab::splitterMoved, this, [this, tab] {
        if (m_settings == nullptr) {
            return;
        }
        const QByteArray state = tab->splitter()->saveState();
        m_settings->setFilterSplitterState(state);
        // Keep the other tabs in line with the last moved splitter.
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto* other = documentTab(i); other != nullptr && other != tab) {
                other->splitter()->restoreState(state);
            }
        }
    });
    m_tabs->setCurrentIndex(index);
    updateEncodingMenu();
    tab->setFocus();
    updateTabState(tab);
    updateStatus();
    return tab;
}

void MainWindow::openFiles(const QStringList& paths)
{
    for (const QString& path : paths) {
        openFile(std::filesystem::path(path.toStdU16String()));
    }
}

void MainWindow::openRequest(const OpenRequest& request)
{
    bool first = true;
    for (const QString& file : request.files) {
        DocumentTab* tab = openFile(std::filesystem::path(file.toStdU16String()));
        if (tab == nullptr) {
            continue;
        }
        if (request.follow) {
            tab->setFollowing(true);
        } else if (first && request.line) {
            // Applied once the file is indexed (the restore path of the tab).
            model::SessionTab state = tab->sessionState();
            state.filter.reset();
            state.topRow = *request.line > 1 ? *request.line - 1 : 0;
            state.currentRow = state.topRow;
            state.following = false;
            tab->restoreSessionState(state);
        }
        first = false;
    }
    if (isMinimized()) {
        showNormal();
    }
    raise();
    activateWindow();
}

model::Session MainWindow::session() const
{
    model::Session result;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (const auto* tab = documentTab(i)) {
            result.tabs.push_back(tab->sessionState());
        }
    }
    result.currentTab = std::max(0, m_tabs->currentIndex());
    result.geometry = saveGeometry();
    result.windowState = saveState();
    return result;
}

int MainWindow::restoreSession(const model::Session& session, bool reopenTabs)
{
    if (!session.geometry.isEmpty()) {
        restoreGeometry(session.geometry);
    }
    if (!session.windowState.isEmpty()) {
        restoreState(session.windowState);
    }
    if (!reopenTabs) {
        return 0;
    }
    QStringList missing;
    int opened = 0;
    int current = -1;
    for (std::size_t i = 0; i < session.tabs.size(); ++i) {
        const model::SessionTab& state = session.tabs[i];
        const std::filesystem::path path(state.path.toStdU16String());
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            missing << QDir::toNativeSeparators(state.path);
            continue;
        }
        if (DocumentTab* tab = openFile(path)) {
            tab->restoreSessionState(state);
            if (static_cast<int>(i) == session.currentTab) {
                current = m_tabs->indexOf(tab);
            }
            ++opened;
        }
    }
    if (current >= 0) {
        m_tabs->setCurrentIndex(current);
    }
    if (!missing.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Not restored (file not found): %1")
                                     .arg(missing.join(QStringLiteral(", "))),
                                 15000);
    }
    return opened;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_settings != nullptr) {
        session().save(m_settings->store());
        m_settings->sync();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::showOpenDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Open Log Files"), QString(),
        QStringLiteral("Log files (*.log *.txt *.out);;All files (*)"));
    openFiles(files);
}

void MainWindow::closeTab(int index)
{
    auto* tab = documentTab(index);
    if (tab == nullptr) {
        return;
    }
    m_tabs->removeTab(index);
    delete tab;  // joins the indexer and releases the file now
    updateStatus();
}

void MainWindow::closeOtherTabs(int index)
{
    auto* keep = documentTab(index);
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        if (documentTab(i) != keep) {
            closeTab(i);
        }
    }
}

void MainWindow::closeAllTabs()
{
    while (m_tabs->count() > 0) {
        closeTab(m_tabs->count() - 1);
    }
}

void MainWindow::updateTabState(DocumentTab* tab)
{
    const int index = m_tabs->indexOf(tab);
    if (index < 0) {
        return;
    }
    using State = model::Document::State;
    const State state = tab->document().state();
    QString title = tab->title();
    if (state == State::kIndexing) {
        title += QStringLiteral(" \u23F3");  // hourglass while indexing (F-TAB-3)
    } else if (state == State::kError || tab->document().isMissing()) {
        title += QStringLiteral(" \u26A0");
    } else if (tab->property("lvChanged").toBool()) {
        title += QStringLiteral(" \u25CF");  // changed while the tab was not active (F-TAB-3)
    }
    m_tabs->setTabText(index, title);
}

QString MainWindow::statusText() const
{
    const auto* tab = currentDocumentTab();
    return tab == nullptr ? QStringLiteral("Ready") : tab->statusText();
}

void MainWindow::createBookmarkActions(QMenu* menu)
{
    m_bookmarksPanel = new BookmarksPanel(this);
    m_bookmarksDock = new QDockWidget(QStringLiteral("Bookmarks"), this);
    m_bookmarksDock->setObjectName(QStringLiteral("bookmarksDock"));
    m_bookmarksDock->setWidget(m_bookmarksPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_bookmarksDock);
    m_bookmarksDock->hide();
    connect(m_tabs, &QTabWidget::currentChanged, this,
            [this] { m_bookmarksPanel->setTab(currentDocumentTab()); });

    menu->addSeparator();
    QAction* panel = m_bookmarksDock->toggleViewAction();
    panel->setObjectName(QStringLiteral("actionBookmarksPanel"));
    panel->setText(QStringLiteral("&Bookmarks Panel"));
    panel->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    menu->addAction(panel);

    const auto add = [this, menu](const QString& name, const QString& text, const QKeySequence& key,
                                  auto handler) {
        QAction* action = menu->addAction(text);
        action->setObjectName(name);
        action->setShortcut(key);
        connect(action, &QAction::triggered, this, [this, handler] {
            if (auto* tab = currentDocumentTab()) {
                handler(tab->activeView());
            }
        });
    };
    // F-MARK-3: mark navigation is a window action, so its keys can be changed (F-SET-5).
    add(QStringLiteral("actionNextMark"), QStringLiteral("Next Mark"),
        QKeySequence(Qt::CTRL | Qt::Key_Down), [](LogView* view) { view->goToNextMark(); });
    add(QStringLiteral("actionPreviousMark"), QStringLiteral("Previous Mark"),
        QKeySequence(Qt::CTRL | Qt::Key_Up), [](LogView* view) { view->goToPreviousMark(); });
    add(QStringLiteral("actionToggleBookmark"), QStringLiteral("Toggle Boo&kmark"),
        QKeySequence(Qt::CTRL | Qt::Key_B), [](LogView* view) { view->toggleBookmark(); });
    add(QStringLiteral("actionNextBookmark"), QStringLiteral("Next Bookmark"),
        QKeySequence(Qt::Key_F2), [](LogView* view) { view->goToNextBookmark(); });
    add(QStringLiteral("actionPreviousBookmark"), QStringLiteral("Previous Bookmark"),
        QKeySequence(Qt::SHIFT | Qt::Key_F2), [](LogView* view) { view->goToPreviousBookmark(); });
    menu->addSeparator();
}

void MainWindow::updateFollowAction()
{
    const auto* tab = currentDocumentTab();
    m_followAction->setChecked(tab != nullptr && tab->isFollowing());
}

void MainWindow::updateStatus()
{
    m_status->setText(statusText());
}

void MainWindow::showTabContextMenu(const QPoint& position)
{
    if (const auto menu = createTabContextMenu(m_tabs->tabBar()->tabAt(position))) {
        menu->exec(m_tabs->tabBar()->mapToGlobal(position));
    }
}

std::unique_ptr<QMenu> MainWindow::createTabContextMenu(int index)
{
    auto* tab = documentTab(index);
    if (tab == nullptr) {
        return nullptr;
    }
    auto menuOwner = std::make_unique<QMenu>(this);
    QMenu& menu = *menuOwner;
    menu.addAction(QStringLiteral("Copy Full Path"), this,
                   [tab] { QApplication::clipboard()->setText(tab->toolTip()); });
    menu.addAction(QStringLiteral("Open Containing Folder"), this, [tab] {
        const QString path = tab->toolTip();
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), path});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
    });
    menu.addAction(QStringLiteral("Reload"), this, [tab] { tab->reload(); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Close"), this, [this, tab] { closeTab(m_tabs->indexOf(tab)); });
    menu.addAction(QStringLiteral("Close Others"), this,
                   [this, tab] { closeOtherTabs(m_tabs->indexOf(tab)); });
    menu.addAction(QStringLiteral("Close All"), this, &MainWindow::closeAllTabs);
    return menuOwner;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_tabs->tabBar() && event->type() == QEvent::MouseButtonRelease) {
        const auto* mouse = dynamic_cast<QMouseEvent*>(event);
        if (mouse != nullptr && mouse->button() == Qt::MiddleButton) {
            const int index = m_tabs->tabBar()->tabAt(mouse->position().toPoint());
            if (index >= 0) {
                closeTab(index);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    openDroppedUrls(event->mimeData()->urls());
    event->acceptProposedAction();
}

void MainWindow::openDroppedUrls(const QList<QUrl>& urls)
{
    QStringList files;
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            files << url.toLocalFile();
        }
    }
    openFiles(files);
}

QString MainWindow::aboutText()
{
    return QStringLiteral("<h3>Log Viewer %1</h3>"
                          "<p>A fast native viewer for large log files.</p>"
                          "<p>Licensed under the MIT License. Qt is used under the GNU LGPL v3.</p>"
                          "<p>Built with Qt %2, running on Qt %3.</p>"
                          "<p>Developed with AI coding agents.</p>")
        .arg(lv::model::versionString(), QStringLiteral(QT_VERSION_STR),
             QString::fromLatin1(qVersion()));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("About Log Viewer"), aboutText());
}

}  // namespace lv::ui
