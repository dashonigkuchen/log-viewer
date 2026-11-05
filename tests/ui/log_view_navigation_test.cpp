#include "fake_line_source.h"
#include "ui/go_to_line_dialog.h"
#include "ui/log_view.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>

#include <cstdint>

using lv::core::LineNumber;
using lv::test::FakeLineSource;

namespace
{

std::uint64_t current(const lv::ui::LogView& view)
{
    return view.currentRow().value_or(LineNumber(UINT64_MAX)).get();
}

void wheel(lv::ui::LogView& view, int angleY, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const QPointF position(50, 50);
    QWheelEvent event(position, view.viewport()->mapToGlobal(position), QPoint(), QPoint(0, angleY),
                      Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &event);
}

}  // namespace

class LogViewNavigationTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        explicit Fixture(std::uint64_t rows)
            : source(rows)
        {
            view.resize(600, 400);
            view.setLineSource(&source);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            view.setFocus();
        }

        FakeLineSource source;
        lv::ui::LogView view;
    };

private slots:

    static void arrowKeysMoveTheCurrentRow()
    {
        Fixture f(1000);
        QCOMPARE(current(f.view), std::uint64_t{0});
        QTest::keyClick(&f.view, Qt::Key_Up);  // stays at the start
        QCOMPARE(current(f.view), std::uint64_t{0});
        QTest::keyClick(&f.view, Qt::Key_Down);
        QTest::keyClick(&f.view, Qt::Key_Down);
        QCOMPARE(current(f.view), std::uint64_t{2});
        QTest::keyClick(&f.view, Qt::Key_Up);
        QCOMPARE(current(f.view), std::uint64_t{1});
        QCOMPARE(f.view.topRow().get(), std::uint64_t{0});
    }

    static void movingPastThePageScrolls()
    {
        Fixture f(1000);
        const std::uint64_t page = f.view.pageRows();
        for (std::uint64_t i = 0; i < page; ++i) {
            QTest::keyClick(&f.view, Qt::Key_Down);
        }
        QCOMPARE(current(f.view), page);
        QCOMPARE(f.view.topRow().get(), std::uint64_t{1});
    }

    static void pageKeysMoveByOnePage()
    {
        Fixture f(1000);
        const std::uint64_t page = f.view.pageRows();
        QTest::keyClick(&f.view, Qt::Key_PageDown);
        QCOMPARE(current(f.view), page);
        QTest::keyClick(&f.view, Qt::Key_PageDown);
        QCOMPARE(current(f.view), 2 * page);
        QTest::keyClick(&f.view, Qt::Key_PageUp);
        QCOMPARE(current(f.view), page);
        QTest::keyClick(&f.view, Qt::Key_PageUp);
        QTest::keyClick(&f.view, Qt::Key_PageUp);
        QCOMPARE(current(f.view), std::uint64_t{0});
    }

    static void controlHomeAndEndJumpToTheFileEnds()
    {
        Fixture f(1000);
        QTest::keyClick(&f.view, Qt::Key_End, Qt::ControlModifier);
        QCOMPARE(current(f.view), std::uint64_t{999});
        QCOMPARE(f.view.topRow().get(), 1000 - f.view.pageRows());
        QTest::keyClick(&f.view, Qt::Key_Down);  // stays at the end
        QCOMPARE(current(f.view), std::uint64_t{999});
        QTest::keyClick(&f.view, Qt::Key_PageDown);
        QCOMPARE(current(f.view), std::uint64_t{999});
        QTest::keyClick(&f.view, Qt::Key_Home, Qt::ControlModifier);
        QCOMPARE(current(f.view), std::uint64_t{0});
        QCOMPARE(f.view.topRow().get(), std::uint64_t{0});
    }

    static void homeAndEndScrollHorizontally()
    {
        Fixture f(10);
        f.source.setLongRow(0, QString(5000, u'x'));
        f.view.viewport()->repaint();
        QVERIFY(f.view.horizontalScrollBar()->maximum() > 0);
        QTest::keyClick(&f.view, Qt::Key_End);
        QCOMPARE(f.view.horizontalOffset(), f.view.horizontalScrollBar()->maximum());
        QCOMPARE(current(f.view), std::uint64_t{0});  // the row does not change
        QTest::keyClick(&f.view, Qt::Key_Left);
        QVERIFY(f.view.horizontalOffset() < f.view.horizontalScrollBar()->maximum());
        QTest::keyClick(&f.view, Qt::Key_Home);
        QCOMPARE(f.view.horizontalOffset(), 0);
        QTest::keyClick(&f.view, Qt::Key_Right);
        QVERIFY(f.view.horizontalOffset() > 0);
    }

    static void keysOnAnEmptySourceDoNothing()
    {
        Fixture f(0);
        for (const auto key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_PageUp, Qt::Key_PageDown,
                               Qt::Key_Home, Qt::Key_End}) {
            QTest::keyClick(&f.view, key);
            QTest::keyClick(&f.view, key, Qt::ControlModifier);
        }
        QVERIFY(!f.view.currentRow().has_value());
        QCOMPARE(f.view.topRow().get(), std::uint64_t{0});
    }

    static void clickingARowMakesItCurrent()
    {
        Fixture f(1000);
        f.view.scrollToRow(LineNumber(100));
        const int y = (f.view.lineHeight() * 5) + (f.view.lineHeight() / 2);
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(100, y));
        QCOMPARE(current(f.view), std::uint64_t{105});

        // Clicking below the last row of a short file keeps the current row.
        const Fixture shortFile(3);
        QTest::mouseClick(shortFile.view.viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(100, shortFile.view.lineHeight() * 10));
        QCOMPARE(current(shortFile.view), std::uint64_t{0});
    }

    static void wheelScrollsBySystemLines()
    {
        Fixture f(1000);
        const auto lines = static_cast<std::uint64_t>(QApplication::wheelScrollLines());
        wheel(f.view, -120);  // one notch down
        QCOMPARE(f.view.topRow().get(), lines);
        wheel(f.view, -60);  // half notches accumulate
        wheel(f.view, -60);
        QCOMPARE(f.view.topRow().get(), 2 * lines);
        wheel(f.view, 1200);  // far up clamps at the start
        QCOMPARE(f.view.topRow().get(), std::uint64_t{0});
        QCOMPARE(current(f.view), std::uint64_t{0});  // the wheel does not move the current row
    }

    static void goToRowCentersTheLine()
    {
        Fixture f(1000);
        f.view.goToRow(LineNumber(500));
        QCOMPARE(current(f.view), std::uint64_t{500});
        QCOMPARE(f.view.topRow().get(), 500 - (f.view.pageRows() / 2));
        f.view.goToRow(LineNumber(5000));  // clamps to the last row
        QCOMPARE(current(f.view), std::uint64_t{999});
    }

    static void goToLineDialogParsesInput()
    {
        using lv::ui::GoToLineDialog;
        QCOMPARE(GoToLineDialog::parseLine(QStringLiteral("42"), 100),
                 std::optional<std::uint64_t>(42));
        QCOMPARE(GoToLineDialog::parseLine(QStringLiteral(" 7 "), 100),
                 std::optional<std::uint64_t>(7));
        QCOMPARE(GoToLineDialog::parseLine(QStringLiteral("100"), 100),
                 std::optional<std::uint64_t>(100));
        QVERIFY(!GoToLineDialog::parseLine(QStringLiteral("0"), 100).has_value());
        QVERIFY(!GoToLineDialog::parseLine(QStringLiteral("101"), 100).has_value());
        QVERIFY(!GoToLineDialog::parseLine(QStringLiteral("-3"), 100).has_value());
        QVERIFY(!GoToLineDialog::parseLine(QStringLiteral("abc"), 100).has_value());
        QVERIFY(!GoToLineDialog::parseLine(QString(), 100).has_value());

        const GoToLineDialog dialog(100, 5);
        QCOMPARE(dialog.lineEdit()->text(), QStringLiteral("5"));
        QVERIFY(dialog.okButton()->isEnabled());
        dialog.lineEdit()->setText(QStringLiteral("500"));
        QVERIFY(!dialog.okButton()->isEnabled());
        dialog.lineEdit()->setText(QStringLiteral("77"));
        QCOMPARE(dialog.line(), std::optional<std::uint64_t>(77));
    }

    static void controlGOpensTheDialogAndJumps()
    {
        const Fixture f(1000);
        auto* action = f.view.findChild<QAction*>(QStringLiteral("actionGoToLine"));
        QVERIFY(action != nullptr);
        QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_G));
        // Fill in and accept the dialog as soon as it opens.
        QTimer::singleShot(0, [] {
            auto* dialog = qobject_cast<lv::ui::GoToLineDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog != nullptr);
            dialog->lineEdit()->setText(QStringLiteral("321"));
            dialog->accept();
        });
        action->trigger();
        QCOMPARE(current(f.view), std::uint64_t{320});  // 1-based input
    }
};

QTEST_MAIN(LogViewNavigationTest)
#include "log_view_navigation_test.moc"
