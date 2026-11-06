#include "log_writer.h"
#include "model/document.h"
#include "model/recent_files.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QElapsedTimer>
#include <QMenu>
#include <QTest>

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
    // Process events at least once, so a pending reload has started.
    QApplication::processEvents();
    while (tab->document().state() != lv::model::Document::State::kReady) {
        if (timer.elapsed() > 10000) {
            return false;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QApplication::processEvents();
    return true;
}

QAction* encodingAction(const lv::ui::MainWindow& window, const QString& text)
{
    for (QAction* action : window.encodingMenu()->actions()) {
        if (action->text().remove(u'&') == text) {
            return action;
        }
    }
    return nullptr;
}

}  // namespace

class MainWindowRecentTest : public QObject
{
    Q_OBJECT

private slots:

    static void openedFilesAppearInTheRecentMenu()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "1\n");
        writeText(dir / "b.log", "2\n");
        lv::ui::MainWindow window;
        window.openFile(dir / "a.log");
        window.openFile(dir / "b.log");
        window.closeAllTabs();
        std::filesystem::remove(dir / "a.log");

        window.updateRecentMenu();
        const auto actions = window.recentMenu()->actions();
        QVERIFY(actions.size() >= 2);
        QVERIFY(actions[0]->text().endsWith(QStringLiteral("b.log")));
        QVERIFY(actions[0]->isEnabled());
        QVERIFY(actions[1]->text().endsWith(QStringLiteral("a.log")));
        QVERIFY(!actions[1]->isEnabled());  // missing file is disabled

        actions[0]->trigger();
        QCOMPARE(window.tabWidget()->count(), 1);
        QCOMPARE(window.currentDocumentTab()->title(), QStringLiteral("b.log"));

        auto* removeMissing =
            window.recentMenu()->findChild<QAction*>(QStringLiteral("actionRemoveMissingRecent"));
        QVERIFY(removeMissing != nullptr);
        removeMissing->trigger();
        QCOMPARE(window.recentFiles()->paths().size(), 1);
    }

    static void encodingMenuReindexesTheFile()
    {
        const lv::test::TempDir dir;
        writeText(dir / "cp.log", "Gr\xF6\xDF"
                                  "e\nsecond\n");  // Windows-1252
        lv::ui::MainWindow window;
        auto* tab = window.openFile(dir / "cp.log");
        QVERIFY(waitReady(tab));
        const auto text = [tab] {
            return tab->mainView()
                ->lineSource()
                ->lines(lv::core::LineNumber(0), lv::core::LineCount(1))[0]
                .text;
        };
        QCOMPARE(text(), QString::fromUtf8("Größe"));
        QVERIFY(encodingAction(window, QStringLiteral("Auto-detect"))->isChecked());

        auto* iso = encodingAction(window, QStringLiteral("UTF-8"));
        QVERIFY(iso != nullptr);
        iso->trigger();
        QVERIFY(iso->isChecked());
        QVERIFY(waitReady(tab));
        QCOMPARE(tab->document().encodingOverride(), std::optional(lv::core::Encoding::kUtf8));
        QCOMPARE(text(), QString::fromUtf8("Gr\uFFFD\uFFFDe"));

        encodingAction(window, QStringLiteral("Auto-detect"))->trigger();
        QVERIFY(waitReady(tab));
        QCOMPARE(text(), QString::fromUtf8("Größe"));
    }

    static void reloadKeepsThePosition()
    {
        const lv::test::TempDir dir;
        std::string content;
        for (int i = 0; i < 2000; ++i) {
            content += "line " + std::to_string(i) + "\n";
        }
        writeText(dir / "big.log", content);
        lv::ui::MainWindow window;
        window.resize(800, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* tab = window.openFile(dir / "big.log");
        QVERIFY(waitReady(tab));
        tab->mainView()->scrollToRow(lv::core::LineNumber(1000));
        tab->mainView()->setCurrentRow(lv::core::LineNumber(1005));

        auto* reload = window.findChild<QAction*>(QStringLiteral("actionReload"));
        QVERIFY(reload != nullptr);
        QCOMPARE(reload->shortcut(), QKeySequence(Qt::Key_F5));
        reload->trigger();
        QVERIFY(waitReady(tab));
        QCOMPARE(tab->mainView()->topRow().get(), std::uint64_t{1000});
        QCOMPARE(tab->mainView()->currentRow().value_or(lv::core::LineNumber(0)).get(),
                 std::uint64_t{1005});
    }
};

QTEST_MAIN(MainWindowRecentTest)
#include "main_window_recent_test.moc"
