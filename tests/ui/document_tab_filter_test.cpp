#include "log_writer.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "model/settings.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QSplitter>
#include <QTest>

#include <string>

namespace
{

void writeText(const std::filesystem::path& path, const std::string& text)
{
    lv::test::LogWriter writer(path, true);
    writer.append(text);
}

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

// 2000 lines; every 10th line (0, 10, 20, ...) contains "hit".
std::string sampleLog()
{
    std::string text;
    for (int i = 0; i < 2000; ++i) {
        text += (i % 10 == 0 ? "hit " : "line ") + std::to_string(i) + "\n";
    }
    return text;
}

bool applyAndWait(lv::ui::DocumentTab& tab, const std::string& text)
{
    if (!waitFor([&] { return tab.document().state() == lv::model::Document::State::kReady; })) {
        return false;
    }
    if (tab.filter().apply(lv::core::Query{.text = text}).has_value()) {
        return false;
    }
    return waitFor(
        [&] { return tab.filter().state() == lv::model::FilteredDocument::State::kDone; });
}

}  // namespace

class DocumentTabFilterTest : public QObject
{
    Q_OBJECT

private slots:

    static void filteredViewShowsMatchesWithOriginalNumbers()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", sampleLog());
        lv::ui::DocumentTab tab(dir / "a.log");
        tab.resize(800, 600);
        QVERIFY(applyAndWait(tab, "hit"));
        auto* filtered = tab.filteredView();
        QCOMPARE(filtered->objectName(), QStringLiteral("filteredView"));
        QCOMPARE(filtered->rowCount().get(), std::uint64_t{200});
        QCOMPARE(filtered->lineSource()->originalLineNumber(lv::core::LineNumber(3)).get(),
                 std::uint64_t{30});
        // The gutter is sized for the original numbers (up to 1991, 4 digits), not for the
        // 200 rows (3 digits): it is as wide as the main view's gutter for 2000 lines.
        QCOMPARE(filtered->gutterWidth(), tab.mainView()->gutterWidth());
        QCOMPARE(tab.splitter()->count(), 2);
        QCOMPARE(tab.splitter()->widget(0), tab.mainView());
        QCOMPARE(tab.splitter()->widget(1)->objectName(), QStringLiteral("filterPane"));
        QVERIFY(tab.splitter()->widget(1)->isAncestorOf(filtered));
    }

    static void clickInFilteredViewMovesTheMainView()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", sampleLog());
        lv::ui::DocumentTab tab(dir / "a.log");
        tab.resize(800, 600);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QVERIFY(applyAndWait(tab, "hit"));
        auto* filtered = tab.filteredView();
        const int y = (filtered->lineHeight() * 4) + (filtered->lineHeight() / 2);
        QTest::mouseClick(filtered->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(80, y));
        QCOMPARE(filtered->currentRow().value_or(lv::core::LineNumber(0)).get(), std::uint64_t{4});
        const auto mainRow = tab.mainView()->currentRow();
        QVERIFY(mainRow.has_value());
        QCOMPARE(mainRow.value_or(lv::core::LineNumber(0)).get(), std::uint64_t{40});
        const auto top = tab.mainView()->topRow().get();
        QVERIFY(top <= 40 && 40 < top + tab.mainView()->pageRows());

        // Keyboard navigation in the filtered view follows too.
        filtered->setFocus();
        QTest::keyClick(filtered, Qt::Key_Down);
        QCOMPARE(tab.mainView()->currentRow().value_or(lv::core::LineNumber(0)).get(),
                 std::uint64_t{50});
    }

    static void movingInTheMainViewSelectsTheNearestMatch()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", sampleLog());
        lv::ui::DocumentTab tab(dir / "a.log");
        tab.resize(800, 600);
        QVERIFY(applyAndWait(tab, "hit"));
        auto* filtered = tab.filteredView();
        tab.mainView()->setCurrentRow(lv::core::LineNumber(1234));
        QCOMPARE(filtered->currentRow().value_or(lv::core::LineNumber(0)).get(),
                 std::uint64_t{123});  // line 1230
        tab.mainView()->setCurrentRow(lv::core::LineNumber(1240));
        QCOMPARE(filtered->currentRow().value_or(lv::core::LineNumber(0)).get(),
                 std::uint64_t{124});  // exact match
        const auto top = filtered->topRow().get();
        QVERIFY(top <= 124 && 124 < top + filtered->pageRows());
        // The main view was not moved by the synchronization.
        QCOMPARE(tab.mainView()->currentRow().value_or(lv::core::LineNumber(0)).get(),
                 std::uint64_t{1240});
    }

    static void noMatchBeforeTheLineKeepsTheFilteredRow()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "a\nb\nhit\nc\n");
        lv::ui::DocumentTab tab(dir / "a.log");
        QVERIFY(applyAndWait(tab, "hit"));
        tab.mainView()->setCurrentRow(lv::core::LineNumber(3));
        QCOMPARE(tab.filteredView()->currentRow().value_or(lv::core::LineNumber(9)).get(),
                 std::uint64_t{0});
        tab.mainView()->setCurrentRow(lv::core::LineNumber(1));  // before the first match
        QCOMPARE(tab.filteredView()->currentRow().value_or(lv::core::LineNumber(9)).get(),
                 std::uint64_t{0});
        QCOMPARE(tab.mainView()->currentRow().value_or(lv::core::LineNumber(0)).get(),
                 std::uint64_t{1});
    }

    static void splitterPositionIsPersisted()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "x\n");
        writeText(dir / "b.log", "y\n");
        lv::model::Settings settings(QString::fromStdString((dir / "settings").string()));
        QByteArray saved;
        {
            lv::ui::MainWindow window;
            window.setSettings(&settings);
            window.resize(900, 700);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto* a = window.openFile(dir / "a.log");
            auto* b = window.openFile(dir / "b.log");
            QApplication::processEvents();
            a->splitter()->setSizes({500, 120});
            QVERIFY(QMetaObject::invokeMethod(a->splitter(), "splitterMoved", Q_ARG(int, 500),
                                              Q_ARG(int, 1)));
            saved = settings.filterSplitterState();
            QVERIFY(!saved.isEmpty());
            QCOMPARE(b->splitter()->saveState(), saved);  // other tabs follow
        }
        lv::ui::MainWindow window;
        window.setSettings(&settings);
        auto* c = window.openFile(dir / "a.log");
        QCOMPARE(c->splitter()->saveState(), saved);
    }
};

QTEST_MAIN(DocumentTabFilterTest)
#include "document_tab_filter_test.moc"
