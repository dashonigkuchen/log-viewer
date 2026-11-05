#include "fake_line_source.h"
#include "ui/log_view.h"

#include <QImage>
#include <QScrollBar>
#include <QTest>

#include <cstdint>
#include <vector>

using lv::core::LineNumber;
using lv::test::FakeLineSource;

namespace
{

QImage render(lv::ui::LogView& view)
{
    QImage image(view.viewport()->size(), QImage::Format_ARGB32);
    view.viewport()->render(&image);
    return image;
}

}  // namespace

class LogViewTest : public QObject
{
    Q_OBJECT

private slots:

    static void showsFirstLinesAtTheTop()
    {
        FakeLineSource source(1000);
        lv::ui::LogView view;
        view.resize(600, 400);
        view.setLineSource(&source);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        (void)render(view);
        QCOMPARE(view.topRow().get(), std::uint64_t{0});
        QCOMPARE(source.lastFirst(), std::uint64_t{0});
        // Only the visible rows (plus one partial) are requested.
        QVERIFY(source.lastCount() <= view.pageRows() + 1);
        QVERIFY(view.currentRow().has_value());
        QCOMPARE(view.currentRow().value_or(LineNumber(99)).get(), std::uint64_t{0});
    }

    static void scrollToRowShowsItAtTheTop()
    {
        FakeLineSource source(1000);
        lv::ui::LogView view;
        view.resize(600, 400);
        view.setLineSource(&source);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.scrollToRow(LineNumber(500));
        (void)render(view);
        QCOMPARE(view.topRow().get(), std::uint64_t{500});
        QCOMPARE(source.lastFirst(), std::uint64_t{500});
        QCOMPARE(view.verticalScrollBar()->value(), 500);

        // Past the end clamps to the last full page.
        view.scrollToRow(LineNumber(5000));
        QCOMPARE(view.topRow().get(), std::uint64_t{1000} - view.pageRows());
    }

    static void scrollBarMovesTheTopRow()
    {
        FakeLineSource source(1000);
        lv::ui::LogView view;
        view.resize(600, 400);
        view.setLineSource(&source);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.verticalScrollBar()->setValue(123);
        QCOMPARE(view.topRow().get(), std::uint64_t{123});
    }

    static void hugeSourcesKeepAnExactTopRow()
    {
        const std::uint64_t rows = std::uint64_t{1} << 40U;
        FakeLineSource source(rows);
        lv::ui::LogView view;
        view.resize(600, 400);
        view.setLineSource(&source);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(view.verticalScrollBar()->maximum(), lv::ui::LogView::kMaxScrollRange);

        const LineNumber target((std::uint64_t{1} << 39U) + 12345);
        view.scrollToRow(target);
        (void)render(view);
        QCOMPARE(view.topRow().get(), target.get());
        QCOMPARE(source.lastFirst(), target.get());

        view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum());
        QCOMPARE(view.topRow().get(), rows - view.pageRows());
        view.verticalScrollBar()->setValue(0);
        QCOMPARE(view.topRow().get(), std::uint64_t{0});
    }

    static void gutterGrowsWithTheLineCount()
    {
        FakeLineSource source(9);
        lv::ui::LogView view;
        view.resize(600, 400);
        view.setLineSource(&source);
        const int small = view.gutterWidth();
        source.setRows(1000000, false);
        QVERIFY(view.gutterWidth() > small);
        view.setLineNumbersVisible(false);
        QCOMPARE(view.gutterWidth(), 0);
    }

    static void tabsAreExpandedAndLongLinesMarked()
    {
        lv::ui::LogView view;
        QCOMPARE(view.displayText(QStringLiteral("a\tb"), false), QStringLiteral("a       b"));
        view.setTabWidth(4);
        QCOMPARE(view.displayText(QStringLiteral("a\tb"), false), QStringLiteral("a   b"));

        view.setColumnLimit(10);
        const QString cut = view.displayText(QString(50, u'x'), false);
        QCOMPARE(cut.size(), 11);
        QCOMPARE(cut.back(), QChar(lv::ui::LogView::kTruncationMarker));
        // Lines cut by the reader get the marker even when they fit.
        QCOMPARE(view.displayText(QStringLiteral("abc"), true).back(),
                 QChar(lv::ui::LogView::kTruncationMarker));
        QCOMPARE(view.displayText(QStringLiteral("abc"), false), QStringLiteral("abc"));
    }

    static void longLinesEnableHorizontalScrolling()
    {
        FakeLineSource source(10);
        source.setLongRow(2, QString(5000, u'w'));
        lv::ui::LogView view;
        view.resize(400, 300);
        view.setLineSource(&source);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        (void)render(view);
        QVERIFY(view.horizontalScrollBar()->maximum() > 0);
        view.horizontalScrollBar()->setValue(200);
        QCOMPARE(view.horizontalOffset(), 200);
    }

    static void currentLineIsHighlighted()
    {
        FakeLineSource source(100);
        lv::ui::LogView view;
        view.resize(400, 300);
        view.setLineSource(&source);
        view.setLineNumbersVisible(false);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setCurrentRow(LineNumber(1));
        const QImage image = render(view);
        // Far right of row 1 (no text there) has the highlight color, row 3 the base color.
        const int x = image.width() - 5;
        const QColor current = image.pixelColor(x, (view.lineHeight() * 3) / 2);
        const QColor other = image.pixelColor(x, (view.lineHeight() * 7) / 2);
        QVERIFY(current != other);
        QCOMPARE(other, view.palette().color(QPalette::Base));
    }

    static void resetReturnsToTheTop()
    {
        FakeLineSource source(1000);
        lv::ui::LogView view;
        view.resize(600, 400);
        view.setLineSource(&source);
        view.scrollToRow(LineNumber(400));
        source.setRows(50, true);
        QCOMPARE(view.topRow().get(), std::uint64_t{0});
        QVERIFY(view.currentRow().has_value());
        QCOMPARE(view.currentRow().value_or(LineNumber(99)).get(), std::uint64_t{0});
    }

    static void emptySourcePaintsNothing()
    {
        FakeLineSource source(0);
        lv::ui::LogView view;
        view.resize(400, 300);
        view.setLineSource(&source);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        (void)render(view);
        QCOMPARE(source.requests(), 0);
        QVERIFY(!view.currentRow().has_value());
    }

    static void paintingOneScreenIsFast()
    {
        // P-UI-1 with the factor 2 relaxation for CI (docs/PERFORMANCE.md §3).
        FakeLineSource source(std::uint64_t{1} << 30U);
        lv::ui::LogView view;
        view.resize(1600, 1000);
        view.setLineSource(&source);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        std::int64_t best = INT64_MAX;
        for (int i = 0; i < 10; ++i) {
            view.scrollToRow(LineNumber(static_cast<std::uint64_t>(i) * 1000));
            (void)render(view);
            best = std::min(best, view.lastPaintMicroseconds());
        }
#if defined(NDEBUG)  // release builds only; sanitizer presets are debug builds
        QVERIFY2(best <= 32000, qPrintable(QStringLiteral("paint took %1 us").arg(best)));
#endif
        QVERIFY(best >= 0);
    }
};

QTEST_MAIN(LogViewTest)
#include "log_view_test.moc"
