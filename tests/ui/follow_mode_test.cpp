#include "log_writer.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QTabWidget>
#include <QTest>
#include <QWheelEvent>

#include <filesystem>
#include <string>

using lv::core::LineNumber;
using lv::model::Document;
using lv::model::FilteredDocument;
using lv::test::LogWriter;
using lv::test::TempDir;
using lv::ui::DocumentTab;
using lv::ui::LogView;

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

std::string lines(int from, int count, const char* word)
{
    std::string text;
    for (int i = from; i < from + count; ++i) {
        text += std::string(word) + " line " + std::to_string(i) + "\n";
    }
    return text;
}

// The last row is current and fully visible.
bool atEnd(const LogView& view)
{
    const std::uint64_t rows = view.rowCount().get();
    return rows > 0 && view.currentRow() == LineNumber(rows - 1) &&
           view.topRow().get() + view.pageRows() >= rows;
}

void wheelUp(LogView& view)
{
    const QPoint position(10, 10);
    QWheelEvent event(position, view.viewport()->mapToGlobal(position), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &event);
}

}  // namespace

class FollowModeTest : public QObject
{
    Q_OBJECT

private slots:

    static void followKeepsTheLastLineVisibleInBothViews()
    {
        const TempDir dir;
        LogWriter writer(dir / "app.log", true);
        writer.append(lines(0, 200, "INFO"));
        DocumentTab tab(dir / "app.log");
        tab.document().setPollInterval(100);
        tab.resize(800, 600);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QVERIFY(waitFor([&] { return tab.document().state() == Document::State::kReady; }));
        QVERIFY(!tab.filter().apply({.text = "error"}).has_value());
        QVERIFY(waitFor([&] { return tab.filter().state() == FilteredDocument::State::kDone; }));

        tab.setFollowing(true);
        QVERIFY(tab.mainView()->isFollowing());
        QVERIFY(tab.filteredView()->isFollowing());
        QVERIFY(atEnd(*tab.mainView()));
        QVERIFY(tab.statusText().contains(QStringLiteral("Following")));

        for (int round = 0; round < 3; ++round) {
            writer.append(lines(1000 + (round * 50), 49, "INFO") + "ERROR appended\n");
            const std::uint64_t expected =
                200 + (std::uint64_t{50} * static_cast<std::uint64_t>(round + 1));
            QVERIFY(waitFor([&] { return tab.mainView()->rowCount().get() == expected; }));
            QVERIFY(waitFor([&] {
                return tab.filteredView()->rowCount().get() ==
                       static_cast<std::uint64_t>(round + 1);
            }));
            QVERIFY(waitFor([&] { return atEnd(*tab.mainView()) && atEnd(*tab.filteredView()); }));
        }
    }

    static void scrollingUpDisablesAndCtrlEndReenables()
    {
        const TempDir dir;
        LogWriter writer(dir / "app.log", true);
        writer.append(lines(0, 300, "INFO"));
        DocumentTab tab(dir / "app.log");
        tab.document().setPollInterval(100);
        tab.resize(800, 600);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QVERIFY(waitFor([&] { return tab.document().state() == Document::State::kReady; }));
        tab.setFollowing(true);
        auto* view = tab.mainView();
        QVERIFY(atEnd(*view));

        wheelUp(*view);
        QVERIFY(!tab.isFollowing());
        QVERIFY(!view->isFollowing());
        const LineNumber top = view->topRow();
        writer.append(lines(300, 20, "INFO"));
        QVERIFY(waitFor([&] { return view->rowCount().get() == 320; }));
        QTest::qWait(100);                          // let the queued linesAppended arrive
        QCOMPARE(view->topRow().get(), top.get());  // stays where the user scrolled to

        view->setFocus();
        QTest::keyClick(view, Qt::Key_End, Qt::ControlModifier);
        QVERIFY(tab.isFollowing());
        QVERIFY(atEnd(*view));
        writer.append(lines(320, 20, "INFO"));
        QVERIFY(waitFor([&] { return view->rowCount().get() == 340 && atEnd(*view); }));

        // Moving up with the keyboard turns it off as well.
        QTest::keyClick(view, Qt::Key_PageUp);
        QVERIFY(!tab.isFollowing());
    }

    static void f10TogglesFollowAndInactiveTabsShowChanges()
    {
        const TempDir dir;
        LogWriter first(dir / "a.log", true);
        first.append(lines(0, 10, "INFO"));
        {
            LogWriter second(dir / "b.log", true);
            second.append(lines(0, 10, "INFO"));
        }
        lv::ui::MainWindow window;
        window.resize(900, 600);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* a = window.openFile(dir / "a.log");
        a->document().setPollInterval(100);
        auto* b = window.openFile(dir / "b.log");
        QVERIFY(waitFor([&] {
            return a->document().state() == Document::State::kReady &&
                   b->document().state() == Document::State::kReady;
        }));
        auto* follow = window.findChild<QAction*>(QStringLiteral("actionFollow"));
        QVERIFY(follow != nullptr);
        QCOMPARE(follow->shortcut(), QKeySequence(Qt::Key_F10));
        QVERIFY(!follow->isChecked());
        follow->trigger();
        QVERIFY(b->isFollowing());
        QVERIFY(follow->isChecked());
        QVERIFY(!a->isFollowing());  // per tab

        auto* tabs = window.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        const int indexA = tabs->indexOf(a);
        QVERIFY(!tabs->tabText(indexA).contains(QChar(0x25CF)));
        first.append(lines(10, 5, "INFO"));  // a is not the current tab
        QVERIFY(waitFor([&] { return tabs->tabText(indexA).contains(QChar(0x25CF)); }));

        tabs->setCurrentIndex(indexA);
        QVERIFY(!tabs->tabText(indexA).contains(QChar(0x25CF)));
        QVERIFY(!follow->isChecked());  // a does not follow
    }

    static void deletionShowsANoticeAndTheMissingState()
    {
        const TempDir dir;
        {
            LogWriter writer(dir / "gone.log", true);
            writer.append(lines(0, 5, "INFO"));
        }
        DocumentTab tab(dir / "gone.log");
        tab.document().setPollInterval(100);
        tab.resize(800, 600);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QVERIFY(waitFor([&] { return tab.document().state() == Document::State::kReady; }));
        QVERIFY(tab.noticeText().isEmpty());
        std::filesystem::remove(dir / "gone.log");
        QVERIFY(waitFor([&] { return !tab.noticeText().isEmpty(); }));
        QVERIFY(tab.noticeText().contains(QStringLiteral("deleted")));
        QVERIFY(tab.statusText().contains(QStringLiteral("File deleted")));
        QCOMPARE(tab.mainView()->rowCount().get(), std::uint64_t{5});  // content stays
        tab.hideNotice();
        QVERIFY(tab.noticeText().isEmpty());
        {
            LogWriter writer(dir / "gone.log", true);
            writer.append("back\n");
        }
        QVERIFY(waitFor([&] { return tab.noticeText().contains(QStringLiteral("reappeared")); }));
        QVERIFY(waitFor([&] { return tab.mainView()->rowCount().get() == 1; }));
        QVERIFY(!tab.statusText().contains(QStringLiteral("File deleted")));
    }
};

QTEST_MAIN(FollowModeTest)
#include "follow_mode_test.moc"
