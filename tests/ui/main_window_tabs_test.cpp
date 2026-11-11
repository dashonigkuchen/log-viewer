#include "core/platform/file.h"
#include "log_writer.h"
#include "model/document.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QScopeGuard>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QUrl>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

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

    static void tabContextMenuActions()
    {
        // F-TAB-4: copy full path, open folder, reload, close, close others, close all.
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "a\n");
        writeText(dir / "b.log", "b\n");
        writeText(dir / "c.log", "c\n");
        lv::ui::MainWindow window;
        auto* a = window.openFile(dir / "a.log");
        QVERIFY(waitReady(a));
        QVERIFY(waitReady(window.openFile(dir / "b.log")));
        QVERIFY(waitReady(window.openFile(dir / "c.log")));
        QVERIFY(window.createTabContextMenu(7) == nullptr);

        const auto trigger = [&window](int index, const QString& text) {
            const auto menu = window.createTabContextMenu(index);
            QVERIFY(menu != nullptr);
            QAction* found = nullptr;
            for (QAction* action : menu->actions()) {
                if (action->text() == text) {
                    found = action;
                }
            }
            QVERIFY2(found != nullptr, qPrintable(text));
            found->trigger();
        };
        QStringList texts;
        const auto firstMenu = window.createTabContextMenu(0);  // keep it alive while iterating
        for (QAction* action : firstMenu->actions()) {
            if (!action->isSeparator()) {
                texts << action->text();
            }
        }
        QCOMPARE(texts, QStringList({QStringLiteral("Copy Full Path"),
                                     QStringLiteral("Open Containing Folder"),
                                     QStringLiteral("Reload"), QStringLiteral("Close"),
                                     QStringLiteral("Close Others"), QStringLiteral("Close All")}));

        trigger(0, QStringLiteral("Copy Full Path"));
        QCOMPARE(QApplication::clipboard()->text(), nativePath(dir / "a.log"));

        const auto generation = a->document().generation();
        trigger(0, QStringLiteral("Reload"));
        QVERIFY(a->document().generation() > generation);
        QVERIFY(waitReady(a));

        trigger(2, QStringLiteral("Close"));
        QCOMPARE(window.tabWidget()->count(), 2);
        trigger(1, QStringLiteral("Close Others"));
        QCOMPARE(window.tabWidget()->count(), 1);
        QCOMPARE(window.tabWidget()->tabText(0), QStringLiteral("b.log"));
        trigger(0, QStringLiteral("Close All"));
        QCOMPARE(window.tabWidget()->count(), 0);
    }

    static void tabsCanBeReordered()
    {
        // F-TAB-2: tabs can be dragged; moving keeps every tab with its document.
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "a\n");
        writeText(dir / "b.log", "b\n");
        lv::ui::MainWindow window;
        auto* a = window.openFile(dir / "a.log");
        auto* b = window.openFile(dir / "b.log");
        QVERIFY(waitReady(a));
        QVERIFY(waitReady(b));
        auto* bar = window.tabWidget()->tabBar();
        QVERIFY(bar->isMovable());
        bar->moveTab(0, 1);
        QCOMPARE(window.documentTab(0), b);
        QCOMPARE(window.documentTab(1), a);
        QCOMPARE(window.tabWidget()->tabText(0), QStringLiteral("b.log"));
        window.closeTab(1);
        QCOMPARE(window.tabWidget()->count(), 1);
        QCOMPARE(window.documentTab(0), b);
    }

    static void indexingShowsLinesProgressAndAnIndicator()
    {
        // F-FILE-4 (lines before indexing ends), F-TAB-3 (indicator), F-TAB-6 (size, progress).
        const lv::test::TempDir dir;
        std::string text;
        for (int i = 0; i < 100000; ++i) {
            text += "line " + std::to_string(i) + " with some padding text\n";  // ~3 MiB
        }
        writeText(dir / "slow.log", text);
        const auto target = dir / "slow.log";
        // Slow down every indexing block after the first (512 KiB) so the test sees the state.
        lv::core::File::setReadFaultHook([target](const std::filesystem::path& path,
                                                  lv::core::ByteOffset offset,
                                                  std::size_t) -> std::optional<lv::core::Error> {
            if (path == target && offset.get() >= std::uint64_t{512} * 1024) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
            return std::nullopt;
        });
        const auto clearHook = qScopeGuard([] { lv::core::File::setReadFaultHook({}); });
        lv::ui::MainWindow window;
        auto* tab = window.openFile(target);
        QElapsedTimer timer;
        timer.start();
        while ((tab->document().lineCount().get() == 0 ||
                !window.statusText().contains(QStringLiteral("Indexing "))) &&
               timer.elapsed() < 10000) {
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        QCOMPARE(tab->document().state(), lv::model::Document::State::kIndexing);
        QVERIFY(tab->document().lineCount().get() > 0);
        QVERIFY(window.tabWidget()->tabText(0).endsWith(QStringLiteral("\u23F3")));
        QVERIFY2(window.statusText().contains(QStringLiteral("Indexing ")),
                 qPrintable(window.statusText()));
        QVERIFY(window.statusText().contains(QStringLiteral("MiB")));
        QVERIFY(waitReady(tab));
        QCOMPARE(window.tabWidget()->tabText(0), QStringLiteral("slow.log"));
        QVERIFY(!window.statusText().contains(QStringLiteral("Indexing")));
        QVERIFY(window.statusText().contains(QStringLiteral("100,000 lines")) ||
                window.statusText().contains(QStringLiteral("100000 lines")));
    }
};

QTEST_MAIN(MainWindowTabsTest)
#include "main_window_tabs_test.moc"
