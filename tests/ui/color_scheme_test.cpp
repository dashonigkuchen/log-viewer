#include "fake_line_source.h"
#include "model/highlight_sets.h"
#include "model/line_highlighter.h"
#include "ui/log_view.h"

#include <QApplication>
#include <QImage>
#include <QPalette>
#include <QTest>

using lv::core::LineNumber;
using lv::test::FakeLineSource;

namespace
{

QPalette lightPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::Window, QColor(240, 240, 240));
    palette.setColor(QPalette::Text, QColor(20, 20, 20));
    palette.setColor(QPalette::Highlight, QColor(0, 120, 215));
    palette.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    return palette;
}

QPalette darkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Base, QColor(30, 30, 30));
    palette.setColor(QPalette::Window, QColor(45, 45, 45));
    palette.setColor(QPalette::Text, QColor(220, 220, 220));
    palette.setColor(QPalette::Highlight, QColor(0, 120, 215));
    palette.setColor(QPalette::AlternateBase, QColor(40, 40, 40));
    return palette;
}

QImage render(lv::ui::LogView& view)
{
    QImage image(view.viewport()->size(), QImage::Format_ARGB32);
    view.viewport()->render(&image);
    return image;
}

// Average lightness of the pixels in text row `row` right of the gutter.
double rowLightness(const QImage& image, const lv::ui::LogView& view, int row)
{
    double sum = 0;
    int count = 0;
    for (int y = row * view.lineHeight(); y < (row + 1) * view.lineHeight(); ++y) {
        for (int x = view.gutterWidth() + 200; x < image.width(); x += 7) {
            sum += static_cast<double>(QColor(image.pixel(x, y)).lightnessF());
            ++count;
        }
    }
    return count > 0 ? sum / count : 0;
}

// Darkest and lightest pixel of a text row, for contrast.
std::pair<double, double> rowRange(const QImage& image, const lv::ui::LogView& view, int row)
{
    double low = 1;
    double high = 0;
    for (int y = row * view.lineHeight(); y < (row + 1) * view.lineHeight(); ++y) {
        for (int x = view.gutterWidth(); x < view.gutterWidth() + 120; ++x) {
            const auto l = static_cast<double>(QColor(image.pixel(x, y)).lightnessF());
            low = std::min(low, l);
            high = std::max(high, l);
        }
    }
    return {low, high};
}

}  // namespace

class ColorSchemeTest : public QObject
{
    Q_OBJECT

private slots:

    static void defaultsFollowThePalette()
    {
        FakeLineSource source(50);
        lv::ui::LogView view;
        view.resize(700, 300);
        view.setLineSource(&source);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        view.setPalette(lightPalette());
        QVERIFY(!view.usesDarkColors());
        const QColor lightMatch = view.matchHighlightColor();
        const QColor lightCurrent = view.currentLineColor();
        view.setCurrentRow(LineNumber(10));
        QImage image = render(view);
        QVERIFY(rowLightness(image, view, 3) > 0.9);  // white background

        view.setPalette(darkPalette());
        QVERIFY(view.usesDarkColors());
        QVERIFY(view.matchHighlightColor() != lightMatch);
        QVERIFY(view.currentLineColor() != lightCurrent);
        QVERIFY(view.markColor().isValid());
        QVERIFY(view.bookmarkColor().lightness() > 128);  // visible on dark
        image = render(view);
        QVERIFY(rowLightness(image, view, 3) < 0.2);  // dark background
        // The current line stays dark enough for light text.
        QVERIFY(rowLightness(image, view, 10) < 0.45);

        // An explicit color overrides both defaults; invalid restores the default.
        view.setMatchHighlightColor(QColor(1, 2, 3));
        QCOMPARE(view.matchHighlightColor(), QColor(1, 2, 3));
        view.setMatchHighlightColor(QColor());
        QVERIFY(view.matchHighlightColor() != QColor(1, 2, 3));
    }

    static void ruleBackgroundsStayReadableInDarkMode()
    {
        // The built-in error rule uses a light red background without a text color.
        FakeLineSource source(20);
        lv::model::HighlightSets sets;
        sets.replace(0, {.name = QStringLiteral("Test"),
                         .rules = {{.pattern = QStringLiteral("line 2"),
                                    .regex = false,
                                    .caseSensitive = true,
                                    .scope = lv::model::HighlightScope::kWholeLine,
                                    .foreground = std::nullopt,
                                    .background = 0xFFFFCDD2U,
                                    .enabled = true}}});
        lv::model::LineHighlighter highlighter;
        highlighter.setSets(&sets);
        for (const QPalette& palette : {lightPalette(), darkPalette()}) {
            lv::ui::LogView view;
            view.resize(700, 300);
            view.setPalette(palette);
            view.setLineSource(&source);
            view.setLineHighlighter(&highlighter);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            view.setCurrentRow(LineNumber(15));
            const QImage image = render(view);
            const auto [low, high] = rowRange(image, view, 2);
            // Dark text on the light red background, in both schemes.
            QVERIFY2(high - low > 0.5, qPrintable(QStringLiteral("contrast %1").arg(high - low)));
            QVERIFY(low < 0.35);
        }
    }
};

QTEST_MAIN(ColorSchemeTest)
#include "color_scheme_test.moc"
