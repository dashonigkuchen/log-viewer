#include "model/model_info.h"
#include "ui/main_window.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTest>
#include <QtVersion>

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:

    static void hasTitleWithVersion()
    {
        const lv::ui::MainWindow window;
        QVERIFY(window.windowTitle().contains(lv::model::versionString()));
    }

    static void hasEmptyTabWidgetAsCentralWidget()
    {
        const lv::ui::MainWindow window;
        QVERIFY(window.tabWidget() != nullptr);
        QCOMPARE(window.centralWidget(), window.tabWidget());
        QCOMPARE(window.tabWidget()->count(), 0);
    }

    static void hasFileAndHelpMenus()
    {
        const lv::ui::MainWindow window;
        QVERIFY(window.findChild<QMenu*>(QStringLiteral("menuFile")) != nullptr);
        QVERIFY(window.findChild<QMenu*>(QStringLiteral("menuHelp")) != nullptr);
        QCOMPARE(window.menuBar()->actions().size(), 2);
    }

    static void openActionHasTheStandardShortcut()
    {
        const lv::ui::MainWindow window;
        const auto* open = window.findChild<QAction*>(QStringLiteral("actionOpen"));
        QVERIFY(open != nullptr);
        QVERIFY(open->isEnabled());
        QCOMPARE(open->shortcut(), QKeySequence(QKeySequence::Open));
    }

    static void exitActionClosesTheWindow()
    {
        lv::ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* exitAction = window.findChild<QAction*>(QStringLiteral("actionExit"));
        QVERIFY(exitAction != nullptr);
        exitAction->trigger();
        QVERIFY(!window.isVisible());
    }

    static void hasAboutActionAndStatusBar()
    {
        const lv::ui::MainWindow window;
        QVERIFY(window.findChild<QAction*>(QStringLiteral("actionAbout")) != nullptr);
        QVERIFY(window.statusBar() != nullptr);
    }

    static void aboutTextNamesVersionLicenseQtAndAgents()
    {
        const QString text = lv::ui::MainWindow::aboutText();
        QVERIFY(text.contains(lv::model::versionString()));
        QVERIFY(text.contains(QStringLiteral("MIT License")));
        QVERIFY(text.contains(QString::fromLatin1(qVersion())));
        QVERIFY(text.contains(QStringLiteral("AI coding agents")));
    }
};

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
