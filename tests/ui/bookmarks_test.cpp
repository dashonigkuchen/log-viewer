#include "log_writer.h"
#include "model/bookmarks.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "temp_dir.h"
#include "ui/bookmarks_panel.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QImage>
#include <QLineEdit>
#include <QTest>
#include <QTreeWidget>

using lv::core::LineNumber;
using lv::model::Document;
using lv::model::FilteredDocument;
using lv::ui::DocumentTab;
using lv::ui::MainWindow;

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
    MainWindow window;
    DocumentTab* tab = nullptr;

    Fixture()
    {
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append("INFO zero\nERROR one\nINFO two\nERROR three\nINFO four\n");
        }
        window.resize(1000, 600);
        window.show();
        tab = window.openFile(dir / "a.log");
    }

    [[nodiscard]] bool ready()
    {
        return tab != nullptr && QTest::qWaitForWindowExposed(&window) &&
               waitFor([this] { return tab->document().state() == Document::State::kReady; });
    }

    [[nodiscard]] QAction* action(const char* name) const
    {
        return window.findChild<QAction*>(QString::fromLatin1(name));
    }

    [[nodiscard]] std::uint64_t currentLine() const
    {
        auto* view = tab->activeView();
        return view->lineSource()
            ->originalLineNumber(view->currentRow().value_or(LineNumber(0)))
            .get();
    }
};

}  // namespace

class BookmarksTest : public QObject
{
    Q_OBJECT

private slots:

    static void ctrlBAddsWithDefaultNameAndPaintsASymbol()
    {
        Fixture f;
        QVERIFY(f.ready());
        auto* view = f.tab->mainView();
        view->setFocus();
        view->setCurrentRow(LineNumber(1));
        QTest::keyClick(view, Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(f.tab->bookmarks().name(LineNumber(1)).value_or(QString()),
                 QStringLiteral("ERROR one"));
        // Symbol in the gutter of row 1 only.
        QVERIFY(view->markColumnWidth() > 0);
        const QImage image = view->viewport()->grab().toImage();
        const int x = view->markColumnWidth() / 2;
        const int h = view->lineHeight();
        QVERIFY(image.pixelColor(x, h + (h / 3)) != image.pixelColor(x, (2 * h) + (h / 3)));
        // Ctrl+B again removes it.
        QTest::keyClick(view, Qt::Key_B, Qt::ControlModifier);
        QVERIFY(f.tab->bookmarks().empty());
    }

    static void f2NavigatesInBothViews()
    {
        Fixture f;
        QVERIFY(f.ready());
        f.tab->bookmarks().add(LineNumber(1), QStringLiteral("one"));
        f.tab->bookmarks().add(LineNumber(4), QStringLiteral("four"));
        auto* view = f.tab->mainView();
        view->setFocus();
        view->setCurrentRow(LineNumber(0));
        QTest::keyClick(view, Qt::Key_F2);
        QCOMPARE(f.currentLine(), std::uint64_t{1});
        QTest::keyClick(view, Qt::Key_F2);
        QCOMPARE(f.currentLine(), std::uint64_t{4});
        QTest::keyClick(view, Qt::Key_F2, Qt::ShiftModifier);
        QCOMPARE(f.currentLine(), std::uint64_t{1});

        // Filtered view (ERROR = lines 1, 3): line 4 is not shown and skipped.
        QVERIFY(!f.tab->filter().apply({.text = "ERROR"}).has_value());
        QVERIFY(waitFor([&] { return f.tab->filter().state() == FilteredDocument::State::kDone; }));
        auto* filtered = f.tab->filteredView();
        filtered->setFocus();
        QVERIFY(waitFor([&] { return filtered->hasFocus(); }));
        filtered->setCurrentRow(LineNumber(1));  // line 3
        QVERIFY(f.tab->activeView() == filtered);
        QTest::keyClick(filtered, Qt::Key_F2, Qt::ShiftModifier);
        QCOMPARE(f.currentLine(), std::uint64_t{1});
        QVERIFY(!filtered->goToNextBookmark());  // line 4 is not in the filter
    }

    static void panelListsRenamesDeletesAndJumps()
    {
        Fixture f;
        QVERIFY(f.ready());
        f.action("actionBookmarksPanel")->trigger();
        QVERIFY(f.window.bookmarksDock()->isVisible());
        f.tab->bookmarks().add(LineNumber(3), QStringLiteral("three"));
        f.tab->bookmarks().add(LineNumber(0), QStringLiteral("zero"));
        QTreeWidget* list = f.window.bookmarksPanel()->list();
        QCOMPARE(list->topLevelItemCount(), 2);
        const QTreeWidgetItem* first = list->topLevelItem(0);
        QCOMPARE(first->text(0), QStringLiteral("1"));
        QCOMPARE(first->text(1), QStringLiteral("zero"));
        QCOMPARE(first->text(2), QStringLiteral("INFO zero"));

        // Double-click jumps.
        QTreeWidgetItem* second = list->topLevelItem(1);
        const QRect rect = list->visualItemRect(second);
        // QAbstractItemView only reports a double-click on the item that got the press.
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QCOMPARE(f.tab->mainView()->currentRow().value_or(LineNumber(0)).get(), std::uint64_t{3});

        // Rename in place.
        second = list->topLevelItem(1);
        list->setCurrentItem(second);
        f.window.bookmarksPanel()->renameSelected();
        auto* editor = list->findChild<QLineEdit*>();
        QVERIFY(editor != nullptr);
        editor->selectAll();
        QTest::keyClicks(editor, QStringLiteral("disk full"));
        QTest::keyClick(editor, Qt::Key_Return);
        // The delegate commits through a queued call.
        QVERIFY(waitFor(
            [&] { return f.tab->bookmarks().name(LineNumber(3)) == QStringLiteral("disk full"); }));

        // Delete.
        list->setCurrentItem(list->topLevelItem(0));
        f.window.bookmarksPanel()->deleteSelected();
        QCOMPARE(list->topLevelItemCount(), 1);
        QVERIFY(!f.tab->bookmarks().contains(LineNumber(0)));
    }

    static void reloadClearsBookmarksWithANotice()
    {
        Fixture f;
        QVERIFY(f.ready());
        f.tab->bookmarks().add(LineNumber(2), QStringLiteral("two"));
        f.tab->reload();
        QVERIFY(f.tab->bookmarks().empty());
        QVERIFY(f.tab->noticeText().contains(QStringLiteral("1 bookmark(s) were cleared")));
    }
};

QTEST_MAIN(BookmarksTest)
#include "bookmarks_test.moc"
