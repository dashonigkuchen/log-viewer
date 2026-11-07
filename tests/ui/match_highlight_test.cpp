#include "log_writer.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "model/match_highlighter.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QImage>
#include <QTest>

#include <string>

using lv::core::LineNumber;
using lv::model::FilteredDocument;

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

bool applyAndWait(lv::ui::DocumentTab& tab, const lv::core::Query& query)
{
    if (!waitFor([&] { return tab.document().state() == lv::model::Document::State::kReady; })) {
        return false;
    }
    if (tab.filter().apply(query).has_value()) {
        return false;
    }
    return waitFor([&] { return tab.filter().state() == FilteredDocument::State::kDone; });
}

// x position where display column `column` of `text` starts in `view`.
double columnX(const lv::ui::LogView& view, const QString& displayed, qsizetype column)
{
    const QFontMetricsF metrics(view.font());
    return view.gutterWidth() + 4 + metrics.horizontalAdvance(displayed.left(column));
}

}  // namespace

class MatchHighlightTest : public QObject
{
    Q_OBJECT

private slots:

    static void highlightsAsciiUmlautsTabsAndMultipleMatches()
    {
        const lv::test::TempDir dir;
        // Line 1: two matches; line 2: umlauts before the match; line 3: tab before the match.
        writeText(dir / "a.log", "none\nerr x err\nÄÖÜ err\n\terr\n");
        lv::ui::DocumentTab tab(dir / "a.log");
        tab.resize(900, 600);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QVERIFY(applyAndWait(tab, {.text = "ERR"}));
        auto* view = tab.mainView();

        QVERIFY(view->matchRects(LineNumber(0)).empty());
        const auto two = view->matchRects(LineNumber(1));
        QCOMPARE(two.size(), std::size_t{2});
        const QString line1 = QStringLiteral("err x err");
        QCOMPARE(qRound(two[0].left()), qRound(columnX(*view, line1, 0)));
        QCOMPARE(qRound(two[0].right()), qRound(columnX(*view, line1, 3)));
        QCOMPARE(qRound(two[1].left()), qRound(columnX(*view, line1, 6)));
        QCOMPARE(qRound(two[1].top()), view->lineHeight());

        const auto umlaut = view->matchRects(LineNumber(2));
        QCOMPARE(umlaut.size(), std::size_t{1});
        const QString line2 = QString::fromUtf8("ÄÖÜ err");
        QCOMPARE(qRound(umlaut[0].left()), qRound(columnX(*view, line2, 4)));
        QCOMPARE(qRound(umlaut[0].right()), qRound(columnX(*view, line2, 7)));

        const auto tabbed = view->matchRects(LineNumber(3));
        QCOMPARE(tabbed.size(), std::size_t{1});
        const QString line3 = QString(8, u' ') + QStringLiteral("err");  // tab width 8
        QCOMPARE(qRound(tabbed[0].left()), qRound(columnX(*view, line3, 8)));

        // The filtered view highlights the same text with original line numbers.
        const auto filtered = tab.filteredView()->matchRects(LineNumber(0));
        QCOMPARE(filtered.size(), std::size_t{2});
    }

    static void highlightIsPainted()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "aaaa MATCH bbbb\n");
        lv::ui::DocumentTab tab(dir / "a.log");
        tab.resize(900, 600);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QVERIFY(applyAndWait(tab, {.text = "match"}));
        auto* view = tab.mainView();
        view->setCurrentRow(LineNumber(0));
        view->setMatchHighlightColor(QColor(0, 200, 0));
        const auto rects = view->matchRects(LineNumber(0));
        QCOMPARE(rects.size(), std::size_t{1});
        const QImage image = view->viewport()->grab().toImage();
        // Sample the top edge of the highlight (above glyphs): it must be the highlight color.
        const QPoint probe(qRound(rects[0].center().x()), qRound(rects[0].top()) + 1);
        QCOMPARE(image.pixelColor(probe), QColor(0, 200, 0));
    }

    static void invertedAndClearedFiltersHighlightNothing()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "hit\nmiss\n");
        lv::ui::DocumentTab tab(dir / "a.log");
        tab.resize(600, 400);
        QVERIFY(applyAndWait(tab, {.text = "hit"}));
        QCOMPARE(tab.mainView()->matchRects(LineNumber(0)).size(), std::size_t{1});
        QVERIFY(applyAndWait(tab, {.text = "hit", .invert = true}));
        QVERIFY(tab.mainView()->matchRects(LineNumber(0)).empty());
        tab.filter().clear();
        QVERIFY(tab.mainView()->matchRects(LineNumber(0)).empty());
        QCOMPARE(tab.highlighter().cacheSize(), std::size_t{0});
    }
};

QTEST_MAIN(MatchHighlightTest)
#include "match_highlight_test.moc"
