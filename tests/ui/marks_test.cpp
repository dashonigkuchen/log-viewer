#include "log_writer.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "model/filtered_line_source.h"
#include "model/mark_set.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/text_selection.h"

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QImage>
#include <QTest>

#include <memory>
#include <vector>

using lv::core::LineNumber;
using lv::model::Document;
using lv::model::FilteredDocument;
using lv::ui::DocumentTab;
using lv::ui::TextSelection;

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

struct Fixture
{
    lv::test::TempDir dir;
    std::unique_ptr<DocumentTab> tab;

    Fixture()
    {
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append("INFO 0\nERROR 1\nINFO 2\nERROR 3\nINFO 4\nINFO 5\n");
        }
        tab = std::make_unique<DocumentTab>(dir / "a.log");
        tab->resize(800, 500);
        tab->show();
    }

    [[nodiscard]] bool ready() const
    {
        return QTest::qWaitForWindowExposed(tab.get()) &&
               waitFor([this] { return tab->document().state() == Document::State::kReady; });
    }

    [[nodiscard]] std::vector<LineNumber> marked() const { return tab->marks().lines(); }
};

}  // namespace

class MarksTest : public QObject
{
    Q_OBJECT

private slots:

    static void mKeyTogglesTheCurrentLineAndSelections()
    {
        const Fixture f;
        QVERIFY(f.ready());
        auto* view = f.tab->mainView();
        view->setFocus();
        view->setCurrentRow(LineNumber(2));
        QTest::keyClick(view, Qt::Key_M);
        QCOMPARE(f.marked(), std::vector{LineNumber(2)});
        QTest::keyClick(view, Qt::Key_M);
        QVERIFY(f.marked().empty());
        // Several selected lines.
        view->setSelection(TextSelection::lines(1, 3));
        QTest::keyClick(view, Qt::Key_M);
        QCOMPARE(f.marked(), (std::vector{LineNumber(1), LineNumber(2), LineNumber(3)}));
    }

    static void clickInTheMarkColumnToggles()
    {
        const Fixture f;
        QVERIFY(f.ready());
        auto* view = f.tab->mainView();
        QVERIFY(view->markColumnWidth() > 0);
        const QPoint point(view->markColumnWidth() / 2, (4 * view->lineHeight()) + 2);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(f.marked(), std::vector{LineNumber(4)});
        // The marked row is painted with the mark color.
        const QImage image = view->viewport()->grab().toImage();
        const QPoint inside(view->gutterWidth() + 200, (4 * view->lineHeight()) + 1);
        QVERIFY(image.pixelColor(inside) != image.pixelColor(QPoint(inside.x(), 1)));
    }

    static void navigationVisitsMarksInBothViews()
    {
        const Fixture f;
        QVERIFY(f.ready());
        f.tab->marks().toggle(std::vector{LineNumber(1), LineNumber(4), LineNumber(5)});
        auto* view = f.tab->mainView();
        view->setFocus();
        view->setCurrentRow(LineNumber(0));
        QTest::keyClick(view, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(view->currentRow().value_or(LineNumber(99)).get(), std::uint64_t{1});
        QTest::keyClick(view, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(view->currentRow().value_or(LineNumber(99)).get(), std::uint64_t{4});
        QTest::keyClick(view, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(view->currentRow().value_or(LineNumber(99)).get(), std::uint64_t{1});
        QVERIFY(!view->goToPreviousMark());  // none before line 1

        // Filtered view (ERROR lines 1 and 3): only marks it shows are visited.
        QVERIFY(!f.tab->filter().apply({.text = "ERROR"}).has_value());
        QVERIFY(waitFor([&] { return f.tab->filter().state() == FilteredDocument::State::kDone; }));
        f.tab->marks().toggle(LineNumber(3));
        auto* filtered = f.tab->filteredView();
        filtered->setCurrentRow(LineNumber(0));  // line 1
        QVERIFY(filtered->goToNextMark());
        QCOMPARE(filtered->currentRow().value_or(LineNumber(99)).get(),
                 std::uint64_t{1});  // line 3
        // Marked lines 4 and 5 are shown in the filtered view (F-FILT-9) and visited ...
        QVERIFY(filtered->goToNextMark());
        QCOMPARE(filtered->lineSource()
                     ->originalLineNumber(filtered->currentRow().value_or(LineNumber(0)))
                     .get(),
                 std::uint64_t{4});
        // ... but skipped when the option is off.
        f.tab->setShowMarksInFilter(false);
        filtered->setCurrentRow(LineNumber(1));  // line 3
        QVERIFY(!filtered->goToNextMark());
        f.tab->setShowMarksInFilter(true);
        // Toggling in the filtered view marks the original line.
        filtered->setFocus();
        filtered->setCurrentRow(LineNumber(0));
        QTest::keyClick(filtered, Qt::Key_M);
        QVERIFY(!f.tab->marks().contains(LineNumber(1)));
    }

    static void clearAsksForConfirmationAndCopyKeepsLineOrder()
    {
        const Fixture f;
        QVERIFY(f.ready());
        auto* view = f.tab->mainView();
        f.tab->marks().toggle(LineNumber(3));
        f.tab->marks().toggle(LineNumber(0));
        QApplication::clipboard()->setText(QStringLiteral("x"));
        view->copyMarkedLines();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("INFO 0\nERROR 3\n"));

        std::size_t asked = 0;
        view->setClearMarksConfirmation([&](std::size_t count) {
            asked = count;
            return false;  // declined
        });
        view->clearMarks();
        QCOMPARE(asked, std::size_t{2});
        QCOMPARE(f.tab->marks().size(), std::size_t{2});
        view->setClearMarksConfirmation([](std::size_t) { return true; });
        view->clearMarks();
        QVERIFY(f.tab->marks().empty());
    }

    static void filteredViewShowsMarkedLinesAndKeepsItsPosition()
    {
        const Fixture f;
        QVERIFY(f.ready());
        QVERIFY(!f.tab->filter().apply({.text = "ERROR"}).has_value());
        QVERIFY(waitFor([&] { return f.tab->filter().state() == FilteredDocument::State::kDone; }));
        auto* filtered = f.tab->filteredView();
        QCOMPARE(filtered->rowCount().get(), std::uint64_t{2});
        filtered->setCurrentRow(LineNumber(1));  // line 3
        f.tab->marks().toggle(LineNumber(0));    // inserted before the current row
        QCOMPARE(filtered->rowCount().get(), std::uint64_t{3});
        // The current row still shows line 3.
        const auto row = filtered->currentRow().value_or(LineNumber(99));
        QCOMPARE(filtered->lineSource()->originalLineNumber(row).get(), std::uint64_t{3});
        QVERIFY(f.tab->filteredSource().isExtraRow(LineNumber(0)));
        // The extra row is painted differently from a match row at the same height.
        const QImage image = filtered->viewport()->grab().toImage();
        const int x = filtered->gutterWidth() + 1;
        QVERIFY(image.pixelColor(x, 2) != image.pixelColor(x, filtered->lineHeight() + 2));
        // Turning the option off removes the row.
        f.tab->setShowMarksInFilter(false);
        QCOMPARE(filtered->rowCount().get(), std::uint64_t{2});
    }

    static void reloadClearsMarksWithANotice()
    {
        const Fixture f;
        QVERIFY(f.ready());
        f.tab->marks().toggle(LineNumber(2));
        f.tab->reload();
        QVERIFY(f.tab->marks().empty());
        QVERIFY(f.tab->noticeText().contains(QStringLiteral("1 mark(s) were cleared")));
    }
};

QTEST_MAIN(MarksTest)
#include "marks_test.moc"
