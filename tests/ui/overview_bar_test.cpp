#include "log_writer.h"
#include "model/bookmarks.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "model/mark_set.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/filter_bar.h"
#include "ui/log_view.h"
#include "ui/overview_bar.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QLineEdit>
#include <QTest>

#include <string>

using lv::core::LineNumber;
using lv::model::Document;
using lv::model::FilteredDocument;
using lv::ui::DocumentTab;
using lv::ui::OverviewBar;

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

// 10000 lines; "ERROR" only on lines 2000..2009 and 9000.
void writeLog(const std::filesystem::path& path)
{
    lv::test::LogWriter writer(path, true);
    std::string text;
    for (int i = 0; i < 10000; ++i) {
        const bool error = (i >= 2000 && i < 2010) || i == 9000;
        text += (error ? "ERROR " : "info ") + std::to_string(i) + "\n";
    }
    writer.append(text);
}

QImage render(QWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32);
    widget.render(&image);
    return image;
}

}  // namespace

class OverviewBarTest : public QObject
{
    Q_OBJECT

private slots:

    static void showsMatchesMarksAndBookmarks()
    {
        const lv::test::TempDir dir;
        writeLog(dir / "a.log");
        DocumentTab tab(dir / "a.log");
        tab.resize(800, 700);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QVERIFY(waitFor([&tab] { return tab.document().state() == Document::State::kReady; }));
        OverviewBar* bar = tab.overviewBar();
        QVERIFY(bar != nullptr);
        QVERIFY(bar->isVisible());
        QVERIFY(bar->height() > 100);

        tab.filterBar()->input()->setText(QStringLiteral("ERROR"));
        QVERIFY(tab.filterBar()->apply());
        QVERIFY(waitFor([&tab] { return tab.filter().state() == FilteredDocument::State::kDone; }));

        const int matchY = bar->yOf(LineNumber(2000));
        QVERIFY(bar->hasMatchAt(matchY));
        QVERIFY(bar->hasMatchAt(bar->yOf(LineNumber(9000))));
        QVERIFY(!bar->hasMatchAt(bar->yOf(LineNumber(5000))));
        QVERIFY(!bar->hasMatchAt(0));

        tab.marks().toggle(LineNumber(5000));
        tab.bookmarks().add(LineNumber(7000), QStringLiteral("here"));
        QTest::qWait(100);  // coalesced repaint

        const QImage image = render(*bar);
        const QColor background = bar->palette().color(QPalette::Window);
        const auto differs = [&](int x, int y) {
            return QColor(image.pixel(x, y)) != background;
        };
        QVERIFY(differs(2, matchY));                        // match tick, left half
        QVERIFY(!differs(2, bar->yOf(LineNumber(5000))) ||  // no match there
                bar->yOf(LineNumber(5000)) == bar->yOf(tab.mainView()->topRow()));
        QVERIFY(differs(OverviewBar::kWidth - 3, bar->yOf(LineNumber(5000))));  // mark, right
        QVERIFY(differs(2, bar->yOf(LineNumber(7000))));  // bookmark, full width
        QVERIFY(differs(OverviewBar::kWidth - 3, bar->yOf(LineNumber(7000))));
    }

    static void clickJumps()
    {
        const lv::test::TempDir dir;
        writeLog(dir / "a.log");
        DocumentTab tab(dir / "a.log");
        tab.resize(800, 700);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QVERIFY(waitFor([&tab] { return tab.document().state() == Document::State::kReady; }));
        OverviewBar* bar = tab.overviewBar();

        const int y = bar->yOf(LineNumber(9000));
        const LineNumber target = bar->lineAt(y);
        QVERIFY(target.get() <= 9000 && target.get() + 100 > 9000);
        QTest::mouseClick(bar, Qt::LeftButton, {}, QPoint(OverviewBar::kWidth / 2, y));
        QCOMPARE(tab.mainView()->currentRow().value_or(LineNumber(0)).get(), target.get());
        QVERIFY(tab.mainView()->topRow().get() <= target.get());
        QVERIFY(tab.mainView()->topRow().get() + tab.mainView()->pageRows() > target.get());

        // Clicking the top goes to the start, the bottom to the end.
        QTest::mouseClick(bar, Qt::LeftButton, {}, QPoint(2, 0));
        QCOMPARE(tab.mainView()->currentRow().value_or(LineNumber(1)).get(), std::uint64_t{0});
        QTest::mouseClick(bar, Qt::LeftButton, {}, QPoint(2, bar->height() - 1));
        QVERIFY(tab.mainView()->currentRow().value_or(LineNumber(0)).get() > 9900);
    }
};

QTEST_MAIN(OverviewBarTest)
#include "overview_bar_test.moc"
