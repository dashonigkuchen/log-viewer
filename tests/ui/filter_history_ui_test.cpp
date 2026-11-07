#include "log_writer.h"
#include "model/document.h"
#include "model/filter_history.h"
#include "model/filtered_document.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/filter_bar.h"
#include "ui/main_window.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QTest>

using lv::core::Query;
using lv::core::QueryMode;
using lv::model::FilteredDocument;

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

struct Fixture
{
    lv::test::TempDir dir;
    lv::model::FilterHistory history;
    std::unique_ptr<lv::ui::DocumentTab> tab;

    Fixture()
    {
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append("INFO a\nERROR b\nWARN c\nERROR d\n");
        }
        tab = std::make_unique<lv::ui::DocumentTab>(dir / "a.log");
        tab->filterBar()->setHistory(&history);
        tab->resize(800, 500);
        tab->show();
    }

    [[nodiscard]] lv::ui::FilterBar& bar() const { return *tab->filterBar(); }

    [[nodiscard]] bool done() const
    {
        return waitFor([this] { return tab->filter().state() == FilteredDocument::State::kDone; });
    }

    [[nodiscard]] bool enter(const QString& text) const
    {
        bar().input()->setText(text);
        QTest::keyClick(bar().input(), Qt::Key_Return);
        return done();
    }
};

}  // namespace

class FilterHistoryUiTest : public QObject
{
    Q_OBJECT

private slots:

    static void appliedFiltersAreAddedNewestFirst()
    {
        Fixture f;
        QVERIFY(waitFor(
            [&] { return f.tab->document().state() == lv::model::Document::State::kReady; }));
        QVERIFY(f.enter(QStringLiteral("ERROR")));
        f.bar().modeSelector()->setCurrentIndex(1);
        QVERIFY(f.enter(QStringLiteral("^WARN")));
        QCOMPARE(f.history.entries().size(), std::size_t{2});
        QCOMPARE(f.history.entries()[0].text, std::string("^WARN"));
        QCOMPARE(f.bar().historyBox()->count(), 2);
        QCOMPARE(f.bar().historyBox()->itemText(1), QStringLiteral("ERROR"));
        // Invalid queries are not stored.
        f.bar().input()->setText(QStringLiteral("("));
        QTest::keyClick(f.bar().input(), Qt::Key_Return);
        QCOMPARE(f.history.entries().size(), std::size_t{2});
    }

    static void dropdownSelectionAppliesTheEntry()
    {
        Fixture f;
        f.history.add({.text = "WARN"});
        f.history.add({.text = "ERROR", .mode = QueryMode::kRegex, .caseSensitive = true});
        QVERIFY(waitFor(
            [&] { return f.tab->document().state() == lv::model::Document::State::kReady; }));
        QVERIFY(QMetaObject::invokeMethod(f.bar().historyBox(), "activated", Q_ARG(int, 0)));
        QVERIFY(f.done());
        QCOMPARE(f.tab->filter().query().value_or(Query{}).text, std::string("ERROR"));
        QCOMPARE(f.bar().modeSelector()->currentText(), QStringLiteral("Regex"));
        QVERIFY(f.bar().caseSensitiveBox()->isChecked());
        QCOMPARE(f.tab->filter().matchCount(), std::uint64_t{2});
        QVERIFY(QMetaObject::invokeMethod(f.bar().historyBox(), "activated", Q_ARG(int, 1)));
        QVERIFY(f.done());
        QCOMPARE(f.tab->filter().matchCount(), std::uint64_t{1});
        QCOMPARE(f.history.entries()[0].text, std::string("WARN"));  // re-applied moves up
    }

    static void upAndDownCycleThroughTheHistory()
    {
        Fixture f;
        f.history.add({.text = "one"});
        f.history.add({.text = "two", .invert = true});
        f.history.add({.text = "three"});
        f.bar().input()->setFocus();
        QTest::keyClick(f.bar().input(), Qt::Key_Up);
        QCOMPARE(f.bar().input()->text(), QStringLiteral("three"));
        QTest::keyClick(f.bar().input(), Qt::Key_Up);
        QCOMPARE(f.bar().input()->text(), QStringLiteral("two"));
        QVERIFY(f.bar().invertBox()->isChecked());
        QTest::keyClick(f.bar().input(), Qt::Key_Up);
        QTest::keyClick(f.bar().input(), Qt::Key_Up);  // stays at the oldest
        QCOMPARE(f.bar().input()->text(), QStringLiteral("one"));
        QTest::keyClick(f.bar().input(), Qt::Key_Down);
        QCOMPARE(f.bar().input()->text(), QStringLiteral("two"));
        QTest::keyClick(f.bar().input(), Qt::Key_Down);
        QTest::keyClick(f.bar().input(), Qt::Key_Down);  // back to the applied (none)
        QVERIFY(f.bar().input()->text().isEmpty());
        QCOMPARE(f.tab->filter().state(), FilteredDocument::State::kInactive);  // never applied
    }

    static void deleteRemovesAnEntryAndClearEmptiesTheHistory()
    {
        const lv::test::TempDir dir;
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append("x\n");
        }
        lv::ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* tab = window.openFile(dir / "a.log");
        auto* history = window.filterHistory();
        history->add({.text = "a"});
        history->add({.text = "b"});
        auto* box = tab->filterBar()->historyBox();
        QCOMPARE(box->count(), 2);
        box->view()->setCurrentIndex(box->model()->index(0, 0));
        QTest::keyClick(box->view(), Qt::Key_Delete);
        QCOMPARE(history->entries().size(), std::size_t{1});
        QCOMPARE(history->entries()[0].text, std::string("a"));
        auto* clear = window.findChild<QAction*>(QStringLiteral("actionClearFilterHistory"));
        QVERIFY(clear != nullptr);
        clear->trigger();
        QVERIFY(history->entries().empty());
        QCOMPARE(box->count(), 0);
    }
};

QTEST_MAIN(FilterHistoryUiTest)
#include "filter_history_ui_test.moc"
