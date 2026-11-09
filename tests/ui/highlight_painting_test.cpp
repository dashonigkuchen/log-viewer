#include "fake_line_source.h"
#include "model/highlight_sets.h"
#include "model/line_highlighter.h"
#include "ui/log_view.h"

#include <QApplication>
#include <QImage>
#include <QTest>

#include <algorithm>
#include <cstdint>

using lv::core::LineNumber;
using lv::model::HighlightRule;
using lv::model::HighlightScope;
using lv::model::HighlightSet;
using lv::model::HighlightSets;
using lv::model::LineHighlighter;
using lv::test::FakeLineSource;

namespace
{

QImage render(lv::ui::LogView& view)
{
    QImage image(view.viewport()->size(), QImage::Format_ARGB32);
    view.viewport()->render(&image);
    return image;
}

HighlightRule rule(const char* pattern, HighlightScope scope, std::optional<lv::model::Argb> fg,
                   std::optional<lv::model::Argb> bg, bool regex = false)
{
    return HighlightRule{.pattern = QString::fromUtf8(pattern),
                         .regex = regex,
                         .caseSensitive = true,
                         .scope = scope,
                         .foreground = fg,
                         .background = bg,
                         .enabled = true};
}

// Number of pixels in `row` of the image (one text line) that have exactly `color`.
int countColor(const QImage& image, const lv::ui::LogView& view, int row, QRgb color)
{
    int count = 0;
    const int top = row * view.lineHeight();
    for (int y = top; y < top + view.lineHeight(); ++y) {
        for (int x = view.gutterWidth(); x < image.width(); ++x) {
            if ((image.pixel(x, y) & 0xFFFFFFU) == (color & 0xFFFFFFU)) {
                ++count;
            }
        }
    }
    return count;
}

}  // namespace

class HighlightPaintingTest : public QObject
{
    Q_OBJECT

private slots:

    static void wholeLineAndMatchOnlyRules()
    {
        // Rows read "line N": "line 3" gets a whole-line rule, the digit "7" a match rule.
        FakeLineSource source(20);
        HighlightSets sets;
        sets.replace(
            0, HighlightSet{
                   .name = QStringLiteral("Test"),
                   .rules = {rule("line 3", HighlightScope::kWholeLine, std::nullopt, 0xFF20C020U),
                             rule("7", HighlightScope::kMatchOnly, 0xFFFFFFFFU, 0xFF2020D0U)}});
        LineHighlighter highlighter;
        highlighter.setSets(&sets);
        lv::ui::LogView view;
        view.resize(600, 400);
        view.setLineSource(&source);
        view.setLineHighlighter(&highlighter);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setCurrentRow(LineNumber(10));  // away from the rows we look at
        QImage image = render(view);

        // Whole line: background across the full width of row 3 only.
        const int wide = image.width() - view.gutterWidth();
        QVERIFY(countColor(image, view, 3, 0xFF20C020U) > wide * view.lineHeight() / 2);
        QCOMPARE(countColor(image, view, 2, 0xFF20C020U), 0);
        // Match only: a small blue box on row 7, not the whole line.
        const int box = countColor(image, view, 7, 0xFF2020D0U);
        QVERIFY(box > 0);
        QVERIFY(box < wide * view.lineHeight() / 4);
        QCOMPARE(countColor(image, view, 6, 0xFF2020D0U), 0);
        // The match rule's text color is drawn inside the box.
        QVERIFY(countColor(image, view, 7, 0xFFFFFFFFU) > 0);

        // Switching the set repaints immediately (F-HL-3).
        const std::size_t other = sets.add(HighlightSet{
            .name = QStringLiteral("Other"),
            .rules = {rule("line 5", HighlightScope::kWholeLine, std::nullopt, 0xFFC02020U)}});
        highlighter.setActiveSet(other);
        image = render(view);
        QCOMPARE(countColor(image, view, 3, 0xFF20C020U), 0);
        QVERIFY(countColor(image, view, 5, 0xFFC02020U) > 0);

        // The current line keeps its rule color (framed instead of filled).
        view.setCurrentRow(LineNumber(5));
        image = render(view);
        QVERIFY(countColor(image, view, 5, 0xFFC02020U) > wide * view.lineHeight() / 2);
    }

    static void paintingWithTwentyRulesIsFast()
    {
        // P-UI-1 (20 highlight rules) with the factor 2 relaxation for CI (PERFORMANCE.md §3).
        FakeLineSource source(std::uint64_t{1} << 30U);
        HighlightSet set{.name = QStringLiteral("Twenty"), .rules = {}};
        for (int i = 0; i < 10; ++i) {
            const QByteArray digit = QByteArray::number(i);
            set.rules.push_back(rule(("line \\d*" + digit + "$").constData(),
                                     HighlightScope::kWholeLine, std::nullopt,
                                     0xFF000000U + static_cast<std::uint32_t>(i * 0x101010), true));
            set.rules.push_back(
                rule(digit.constData(), HighlightScope::kMatchOnly, 0xFFFF0000U, std::nullopt));
        }
        HighlightSets sets;
        sets.replace(0, set);
        LineHighlighter highlighter;
        highlighter.setSets(&sets);
        lv::ui::LogView view;
        view.resize(1600, 1000);
        view.setLineSource(&source);
        view.setLineHighlighter(&highlighter);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        std::int64_t worst = 0;
        for (int i = 1; i <= 10; ++i) {
            // New rows every frame (paging): nothing is cached.
            view.scrollToRow(LineNumber(static_cast<std::uint64_t>(i) * 100000));
            (void)render(view);
            worst = std::max(worst, view.lastPaintMicroseconds());
        }
#if defined(NDEBUG)  // release builds only; sanitizer presets are debug builds
        QVERIFY2(worst <= 32000, qPrintable(QStringLiteral("paint took %1 us").arg(worst)));
#endif
        qInfo("worst paint with 20 rules: %lld us", static_cast<long long>(worst));
        QVERIFY(worst >= 0);
    }
};

QTEST_MAIN(HighlightPaintingTest)
#include "highlight_painting_test.moc"
