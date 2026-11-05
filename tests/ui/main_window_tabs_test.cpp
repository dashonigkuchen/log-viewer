#include "log_writer.h"
#include "model/document.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPointer>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QUrl>

#include <string>

namespace
{

void writeText(const std::filesystem::path& path, const std::string& text)
{
    lv::test::LogWriter writer(path, true);
    writer.append(text);
}

bool waitReady(lv::ui::DocumentTab* tab)
{
    QElapsedTimer timer;
    timer.start();
    while (tab->document().state() != lv::model::Document::State::kReady) {
        if (timer.elapsed() > 10000) {
            return false;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QApplication::processEvents();
    return true;
}

QString nativePath(const std::filesystem::path& path)
{
    return QString::fromStdString(lv::ui::normalizedPath(path).string());
}

}  // namespace

class MainWindowTabsTest : public QObject
{
    Q_OBJECT

private slots:

    static void opensFilesInTabsWithTitlesAndToolTips()
    {
        const lv::test::TempDir dir;
        writeText(dir / "first.log", "a\nb\n");
        writeText(dir / "second.log", "one\ntwo\nthree\n");
        lv::ui::MainWindow window;
        auto* first = window.openFile(dir / "first.log");
        auto* second = window.openFile(dir / "second.log");
        QVERIFY(waitReady(first));
        QVERIFY(waitReady(second));
        QCOMPARE(window.tabWidget()->count(), 2);
        QCOMPARE(window.tabWidget()->tabText(0), QStringLiteral("first.log"));
        QCOMPARE(window.tabWidget()->tabText(1), QStringLiteral("second.log"));
        QVERIFY(window.tabWidget()->tabToolTip(1).endsWith(QStringLiteral("second.log")));
        QCOMPARE(window.currentDocumentTab(), second);
        QVERIFY(window.statusText().contains(QStringLiteral("3 lines")));
        QVERIFY(window.statusText().contains(QStringLiteral("UTF-8")));
    }

    static void openingTheSameFileActivatesItsTab()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "x\n");
        writeText(dir / "b.log", "y\n");
        lv::ui::MainWindow window;
        auto* a = window.openFile(dir / "a.log");
        window.openFile(dir / "b.log");
        // Same file through a different spelling of the path.
        auto* again = window.openFile(dir.path() / "." / "a.log");
        QCOMPARE(again, a);
        QCOMPARE(window.tabWidget()->count(), 2);
        QCOMPARE(window.tabWidget()->currentIndex(), 0);
    }

    static void commandLineAndDropUseTheSamePath()
    {
        const lv::test::TempDir dir;
        writeText(dir / "c.log", "1\n");
        writeText(dir / "d.log", "2\n");
        lv::ui::MainWindow window;
        window.openFiles({nativePath(dir / "c.log")});
        QCOMPARE(window.tabWidget()->count(), 1);

        QVERIFY(window.acceptDrops());
        window.openDroppedUrls({QUrl::fromLocalFile(nativePath(dir / "d.log")),
                                QUrl(QStringLiteral("https://example.com/remote.log")),
                                QUrl::fromLocalFile(nativePath(dir / "c.log"))});
        QCOMPARE(window.tabWidget()->count(), 2);
        QCOMPARE(window.currentDocumentTab()->title(), QStringLiteral("c.log"));
    }

    static void closingTabsReleasesDocuments()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "x\n");
        writeText(dir / "b.log", "y\n");
        writeText(dir / "c.log", "z\n");
        lv::ui::MainWindow window;
        const QPointer<lv::ui::DocumentTab> a = window.openFile(dir / "a.log");
        const QPointer<lv::ui::DocumentTab> b = window.openFile(dir / "b.log");
        const QPointer<lv::ui::DocumentTab> c = window.openFile(dir / "c.log");
        window.closeTab(0);
        QVERIFY(a.isNull());
        QCOMPARE(window.tabWidget()->count(), 2);
        window.closeOtherTabs(window.tabWidget()->indexOf(c));
        QVERIFY(b.isNull());
        QCOMPARE(window.tabWidget()->count(), 1);
        auto* closeAction = window.findChild<QAction*>(QStringLiteral("actionClose"));
        QVERIFY(closeAction != nullptr);
        QCOMPARE(closeAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_W));
        closeAction->trigger();
        QVERIFY(c.isNull());
        QCOMPARE(window.tabWidget()->count(), 0);
        QCOMPARE(window.statusText(), QStringLiteral("Ready"));
    }

    static void closeAllAndMiddleClick()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "x\n");
        writeText(dir / "b.log", "y\n");
        lv::ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.openFile(dir / "a.log");
        window.openFile(dir / "b.log");
        auto* bar = window.tabWidget()->tabBar();
        QTest::mouseClick(bar, Qt::MiddleButton, Qt::NoModifier, bar->tabRect(0).center());
        QCOMPARE(window.tabWidget()->count(), 1);
        QCOMPARE(window.currentDocumentTab()->title(), QStringLiteral("b.log"));
        window.closeAllTabs();
        QCOMPARE(window.tabWidget()->count(), 0);
    }

    static void controlTabSwitchesTabs()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "x\n");
        writeText(dir / "b.log", "y\n");
        lv::ui::MainWindow window;
        window.openFile(dir / "a.log");
        window.openFile(dir / "b.log");
        auto* next = window.findChild<QAction*>(QStringLiteral("actionNextTab"));
        auto* previous = window.findChild<QAction*>(QStringLiteral("actionPreviousTab"));
        QVERIFY(next != nullptr && previous != nullptr);
        QCOMPARE(window.tabWidget()->currentIndex(), 1);
        next->trigger();
        QCOMPARE(window.tabWidget()->currentIndex(), 0);
        previous->trigger();
        QCOMPARE(window.tabWidget()->currentIndex(), 1);
    }

    static void missingFilesShowAnError()
    {
        const lv::test::TempDir dir;
        lv::ui::MainWindow window;
        auto* tab = window.openFile(dir / "missing.log");
        QElapsedTimer timer;
        timer.start();
        while (tab->document().state() != lv::model::Document::State::kError &&
               timer.elapsed() < 5000) {
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        QVERIFY(window.statusText().startsWith(QStringLiteral("Error:")));
        QVERIFY(window.tabWidget()->tabText(0).startsWith(QStringLiteral("missing.log")));
    }

    static void statusShowsTheCurrentLine()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "1\n2\n3\n4\n");
        lv::ui::MainWindow window;
        auto* tab = window.openFile(dir / "a.log");
        QVERIFY(waitReady(tab));
        tab->mainView()->setCurrentRow(lv::core::LineNumber(2));
        QVERIFY(window.statusText().contains(QStringLiteral("Line 3")));
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("documentStatus"))
                    ->text()
                    .contains(QStringLiteral("Line 3")));
    }
};

QTEST_MAIN(MainWindowTabsTest)
#include "main_window_tabs_test.moc"
