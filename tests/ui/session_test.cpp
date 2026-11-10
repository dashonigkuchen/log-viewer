#include "log_writer.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "model/session.h"
#include "model/settings.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/filter_bar.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTest>

#include <memory>
#include <string>

using lv::core::LineNumber;
using lv::model::Document;
using lv::model::FilteredDocument;
using lv::ui::DocumentTab;
using lv::ui::MainWindow;

namespace
{

template <typename Condition>
bool waitFor(Condition condition, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    QApplication::processEvents();
    return true;
}

void writeLog(const std::filesystem::path& path, int lines)
{
    lv::test::LogWriter writer(path, true);
    std::string text;
    for (int i = 0; i < lines; ++i) {
        text += (i % 10 == 0 ? "ERROR line " : "INFO line ") + std::to_string(i) + "\n";
    }
    writer.append(text);
}

bool ready(const DocumentTab* tab)
{
    return tab != nullptr &&
           waitFor([tab] { return tab->document().state() == Document::State::kReady; });
}

}  // namespace

class SessionTest : public QObject
{
    Q_OBJECT

private slots:

    static void tabsFiltersAndPositionsAreRestored()
    {
        const lv::test::TempDir dir;
        writeLog(dir / "a.log", 2000);
        writeLog(dir / "b.log", 100);
        writeLog(dir / "gone.log", 10);
        Settings settingsHolder(dir);
        {
            MainWindow window;
            window.setSettings(&settingsHolder.settings);
            window.resize(900, 600);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto* a = window.openFile(dir / "a.log");
            QVERIFY(ready(a));
            a->mainView()->setCurrentRow(LineNumber(1500));
            a->mainView()->scrollToRow(LineNumber(1400));
            a->filterBar()->input()->setText(QStringLiteral("ERROR"));
            QVERIFY(a->filterBar()->apply());
            auto* b = window.openFile(dir / "b.log");
            QVERIFY(ready(b));
            b->setFollowing(true);
            QVERIFY(ready(window.openFile(dir / "gone.log")));
            window.findChild<QTabWidget*>()->setCurrentIndex(0);
            window.close();  // saves the session
        }
        std::filesystem::remove(dir / "gone.log");

        // A new main window object restores the session.
        MainWindow window;
        window.setSettings(&settingsHolder.settings);
        const auto session = lv::model::Session::load(settingsHolder.settings.store());
        QCOMPARE(session.tabs.size(), std::size_t{3});
        QCOMPARE(window.restoreSession(session), 2);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* tabs = window.findChild<QTabWidget*>();
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->currentIndex(), 0);
        auto* a = window.documentTab(0);
        QVERIFY(ready(a));
        QVERIFY(waitFor([a] { return a->filter().state() == FilteredDocument::State::kDone; }));
        QCOMPARE(a->filter().query().value_or(lv::core::Query{}).text, std::string("ERROR"));
        QCOMPARE(a->filterBar()->input()->text(), QStringLiteral("ERROR"));
        QCOMPARE(a->mainView()->currentRow().value_or(LineNumber(0)).get(), std::uint64_t{1500});
        QCOMPARE(a->mainView()->topRow().get(), std::uint64_t{1400});
        auto* b = window.documentTab(1);
        QVERIFY(ready(b));
        QVERIFY(b->isFollowing());
        // The missing file is reported.
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("gone.log")));
    }

    static void windowGeometryIsRestoredWithoutTabs()
    {
        const lv::test::TempDir dir;
        Settings settingsHolder(dir);
        {
            MainWindow window;
            window.setSettings(&settingsHolder.settings);
            window.resize(777, 555);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            window.close();
        }
        MainWindow window;
        // reopenTabs = false: only the layout (session restore disabled).
        QCOMPARE(
            window.restoreSession(lv::model::Session::load(settingsHolder.settings.store()), false),
            0);
        QCOMPARE(window.size(), QSize(777, 555));
    }

private:
    struct Settings
    {
        explicit Settings(const lv::test::TempDir& dir)
            : settings(QString::fromStdU16String((dir / "settings").u16string()))
        {}

        lv::model::Settings settings;
    };
};

QTEST_MAIN(SessionTest)
#include "session_test.moc"
