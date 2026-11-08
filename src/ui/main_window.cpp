#include "ui/main_window.h"

#include "model/document.h"
#include "model/filter_history.h"
#include "model/model_info.h"
#include "model/recent_files.h"
#include "model/settings.h"
#include "ui/document_tab.h"
#include "ui/filter_bar.h"
#include "ui/find_bar.h"
#include "ui/log_view.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
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
          core::Encoding::kWindows1252, core::Encoding::kIso88591}) {
        addEncoding(QString::fromUtf8(core::displayName(encoding).data()), encoding);
    }
    updateEncodingMenu();

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->setObjectName(QStringLiteral("menuHelp"));

    QAction* aboutAction = helpMenu->addAction(QStringLiteral("&About Log Viewer"));
    aboutAction->setObjectName(QStringLiteral("actionAbout"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
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
        m_lineNumbersAction->setChecked(m_settings->lineNumbersVisible());
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
    for (int i = 0; i < m_tabs->count(); ++i) {
        applyViewSettings(documentTab(i));
    }
}

void MainWindow::applyViewSettings(DocumentTab* tab)
{
    if (tab == nullptr) {
        return;
    }
    for (LogView* view : {tab->mainView(), tab->filteredView()}) {
        if (m_settings != nullptr) {
            view->setFont(viewFont());
            view->setTabWidth(m_settings->tabWidth());
            view->setLineNumbersVisible(m_settings->lineNumbersVisible());
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
    } else if (state == State::kError) {
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
    const int index = m_tabs->tabBar()->tabAt(position);
    auto* tab = documentTab(index);
    if (tab == nullptr) {
        return;
    }
    QMenu menu(this);
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
    menu.exec(m_tabs->tabBar()->mapToGlobal(position));
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
