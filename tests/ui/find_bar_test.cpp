#include "log_writer.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/find_bar.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>

#include <string>

using lv::core::LineNumber;
using lv::model::FilteredDocument;
using lv::ui::FindBar;

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
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QApplication::processEvents();
    return true;
}

// Lines: 0 a, 1 ERROR one, 2 b, 3 WARN x, 4 ERROR two, 5 c, 6 WARN error three
constexpr const char* kText = "a\nERROR one\nb\nWARN x\nERROR two\nc\nWARN error three\n";

struct Fixture
{
    lv::test::TempDir dir;
    std::unique_ptr<lv::ui::DocumentTab> tab;

    Fixture()
    {
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append(kText);
        }
        tab = std::make_unique<lv::ui::DocumentTab>(dir / "a.log");
        tab->resize(900, 600);
        tab->show();
    }

    [[nodiscard]] FindBar& bar() const { return *tab->findBar(); }

    [[nodiscard]] bool ready() const
    {
        return waitFor(
            [this] { return tab->document().state() == lv::model::Document::State::kReady; });
    }

    // Runs one search and waits for its result.
    [[nodiscard]] bool step(bool forward) const
    {
        const QSignalSpy finished(&bar(), &FindBar::searchFinished);
        if (forward) {
            bar().findNext();
        } else {
            bar().findPrevious();
        }
        return waitFor([&] { return finished.count() > 0; });
    }

    [[nodiscard]] std::uint64_t mainRow() const
    {
        return tab->mainView()->currentRow().value_or(LineNumber(~std::uint64_t{0})).get();
    }
};

}  // namespace

class FindBarTest : public QObject
{
    Q_OBJECT

private slots:

    static void nextAndPreviousInTheMainViewWrapAround()
    {
        const Fixture f;
        QVERIFY(f.ready());
        f.tab->mainView()->setFocus();
        f.tab->openFindBar();
        QVERIFY(f.bar().isVisible());
        QCOMPARE(f.bar().target(), f.tab->mainView());
        f.bar().input()->setText(QStringLiteral("error"));
        f.tab->mainView()->setCurrentRow(LineNumber(0));
        QVERIFY(f.step(true));
        QCOMPARE(f.mainRow(), std::uint64_t{1});
        QVERIFY(f.bar().statusLabel()->text().isEmpty());
        // The match in the line is selected.
        QCOMPARE(f.tab->mainView()->selectedText(), QStringLiteral("ERROR"));
        QVERIFY(f.step(true));
        QCOMPARE(f.mainRow(), std::uint64_t{4});
        QVERIFY(f.step(true));
        QCOMPARE(f.mainRow(), std::uint64_t{6});
        QVERIFY(f.step(true));
        QCOMPARE(f.mainRow(), std::uint64_t{1});
        QVERIFY(f.bar().statusLabel()->text().contains(QStringLiteral("continued from the top")));
        QVERIFY(f.step(false));
        QCOMPARE(f.mainRow(), std::uint64_t{6});
        QVERIFY(
            f.bar().statusLabel()->text().contains(QStringLiteral("continued from the bottom")));
        QVERIFY(f.step(false));
        QCOMPARE(f.mainRow(), std::uint64_t{4});
        // Case sensitive: only the lower-case "error" in line 6.
        f.bar().caseSensitiveBox()->setChecked(true);
        QVERIFY(f.step(true));
        QCOMPARE(f.mainRow(), std::uint64_t{6});
    }

    static void enterAndShiftEnterInTheField()
    {
        const Fixture f;
        QVERIFY(f.ready());
        f.tab->openFindBar();
        f.bar().input()->setText(QStringLiteral("WARN"));
        f.tab->mainView()->setCurrentRow(LineNumber(4));
        QSignalSpy finished(&f.bar(), &FindBar::searchFinished);
        QTest::keyClick(f.bar().input(), Qt::Key_Return);
        QVERIFY(waitFor([&] { return finished.count() == 1; }));
        QCOMPARE(f.mainRow(), std::uint64_t{6});
        QTest::keyClick(f.bar().input(), Qt::Key_Return, Qt::ShiftModifier);
        QVERIFY(waitFor([&] { return finished.count() == 2; }));
        QCOMPARE(f.mainRow(), std::uint64_t{3});
        QTest::keyClick(f.bar().input(), Qt::Key_Escape);
        QVERIFY(f.bar().isHidden());
        QVERIFY(f.tab->mainView()->findMatchRects(LineNumber(3)).empty());
    }

    static void searchesTheFilteredViewWithinTheFilter()
    {
        const Fixture f;
        QVERIFY(f.ready());
        QVERIFY(!f.tab->filter().apply({.text = "WARN"}).has_value());
        QVERIFY(waitFor([&] { return f.tab->filter().state() == FilteredDocument::State::kDone; }));
        auto* filtered = f.tab->filteredView();
        filtered->setFocus();
        QVERIFY(waitFor([&] { return filtered->hasFocus(); }));
        f.tab->openFindBar();
        QCOMPARE(f.bar().target(), filtered);
        f.bar().input()->setText(QStringLiteral("error"));
        QVERIFY(f.step(true));
        // Lines 1 and 4 match "error" but are not in the filter; line 6 is filtered row 1.
        QCOMPARE(filtered->currentRow().value_or(LineNumber(~std::uint64_t{0})).get(),
                 std::uint64_t{1});
        QCOMPARE(f.mainRow(), std::uint64_t{6});  // views follow each other
        QVERIFY(f.step(true));
        QCOMPARE(filtered->currentRow().value_or(LineNumber(~std::uint64_t{0})).get(),
                 std::uint64_t{1});
        QVERIFY(f.bar().statusLabel()->text().contains(QStringLiteral("continued")));
        f.bar().input()->setText(QStringLiteral("one"));
        QVERIFY(f.step(true));
        QCOMPARE(f.bar().statusLabel()->text(), QStringLiteral("No matches"));
    }

    static void invalidRegexShowsAnErrorAndDoesNotSearch()
    {
        const Fixture f;
        QVERIFY(f.ready());
        f.tab->openFindBar();
        f.bar().regexBox()->setChecked(true);
        QVERIFY(!f.bar().wholeWordBox()->isEnabled());
        f.bar().input()->setText(QStringLiteral("ERROR("));
        QVERIFY(f.bar().errorLabel()->isVisible());
        QVERIFY(f.bar().errorLabel()->text().startsWith(QStringLiteral("Error at position")));
        f.tab->mainView()->setCurrentRow(LineNumber(0));
        f.bar().findNext();
        QVERIFY(!f.bar().isSearching());
        QCOMPARE(f.mainRow(), std::uint64_t{0});
        f.bar().input()->setText(QStringLiteral("ERROR (t|o)"));
        QVERIFY(f.bar().errorLabel()->isHidden());
        QVERIFY(f.step(true));
        QCOMPARE(f.mainRow(), std::uint64_t{1});
    }

    static void visibleMatchesAreHighlightedInBothViews()
    {
        const Fixture f;
        QVERIFY(f.ready());
        QVERIFY(!f.tab->filter().apply({.text = "ERROR"}).has_value());
        QVERIFY(waitFor([&] { return f.tab->filter().state() == FilteredDocument::State::kDone; }));
        f.tab->openFindBar();
        f.bar().input()->setText(QStringLiteral("o"));
        // "ERROR one": two o's ("ERROR" has one 'O' case-insensitively plus "one").
        QCOMPARE(f.tab->mainView()->findMatchRects(LineNumber(1)).size(), std::size_t{2});
        QVERIFY(f.tab->mainView()->findMatchRects(LineNumber(0)).empty());
        // Filtered row 1 is "ERROR two": the O of ERROR and the o of two.
        QCOMPARE(f.tab->filteredView()->findMatchRects(LineNumber(1)).size(), std::size_t{2});
        f.bar().close();
        QVERIFY(f.tab->mainView()->findMatchRects(LineNumber(1)).empty());
        QVERIFY(f.bar().isHidden());
    }

    static void shortcutsAreRegistered()
    {
        const lv::ui::MainWindow window;
        const auto* find = window.findChild<QAction*>(QStringLiteral("actionFind"));
        const auto* next = window.findChild<QAction*>(QStringLiteral("actionFindNext"));
        const auto* previous = window.findChild<QAction*>(QStringLiteral("actionFindPrevious"));
        QVERIFY(find != nullptr && next != nullptr && previous != nullptr);
        QCOMPARE(find->shortcut(), QKeySequence(QKeySequence::Find));
        QCOMPARE(next->shortcut(), QKeySequence(Qt::Key_F3));
        QCOMPARE(previous->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F3));
    }
};

QTEST_MAIN(FindBarTest)
#include "find_bar_test.moc"
