#include "fake_line_source.h"
#include "log_writer.h"
#include "loggen/loggen.h"
#include "model/document.h"
#include "model/document_line_source.h"
#include "temp_dir.h"
#include "ui/log_view.h"
#include "ui/text_selection.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QTest>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using lv::core::LineNumber;
using lv::ui::TextPosition;
using lv::ui::TextSelection;

namespace
{

// A source with fixed texts per row.
class TextLineSource : public lv::model::LineSource
{
public:
    explicit TextLineSource(std::vector<QString> rows, bool lastPartial = false)
        : m_rows(std::move(rows))
        , m_lastPartial(lastPartial)
    {}

    [[nodiscard]] lv::core::LineCount lineCount() const override
    {
        return lv::core::LineCount(m_rows.size());
    }

    [[nodiscard]] LineNumber originalLineNumber(LineNumber row) const override { return row; }

    [[nodiscard]] std::vector<lv::model::DisplayLine>
    lines(LineNumber first, lv::core::LineCount count) const override
    {
        std::vector<lv::model::DisplayLine> out;
        for (std::uint64_t i = 0; i < count.get(); ++i) {
            lv::model::DisplayLine line;
            const auto row = static_cast<std::size_t>(first.get() + i);
            line.text = m_rows.at(row);
            line.partial = m_lastPartial && row + 1 == m_rows.size();
            out.push_back(std::move(line));
        }
        return out;
    }

    [[nodiscard]] std::vector<lv::model::DisplayLine>
    plainLines(LineNumber first, lv::core::LineCount count) const override
    {
        return lines(first, count);
    }

private:
    std::vector<QString> m_rows;
    bool m_lastPartial;
};

struct Fixture
{
    explicit Fixture(std::vector<QString> rows, bool lastPartial = false)
        : source(std::move(rows), lastPartial)
    {
        view.resize(800, 400);
        view.setLineSource(&source);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setFocus();
    }

    [[nodiscard]] QPoint at(std::uint64_t row, qsizetype column) const
    {
        return view.pointForPosition({.row = row, .column = column});
    }

    TextLineSource source;
    lv::ui::LogView view;
};

const QString kUmlauts = QStringLiteral("Größe\tÜbel ärgert 😀 end");

}  // namespace

class LogViewSelectionTest : public QObject
{
    Q_OBJECT

private slots:

    static void columnsForRowInCharacterMode()
    {
        const auto selection =
            TextSelection::characters({.row = 3, .column = 4}, {.row = 1, .column = 2});
        QCOMPARE(selection.start().row, std::uint64_t{1});
        QVERIFY(!selection.columnsForRow(0, 10).has_value());
        const auto first = selection.columnsForRow(1, 10).value_or(lv::ui::SelectedColumns{});
        QCOMPARE(first.from, qsizetype{2});
        QCOMPARE(first.to, qsizetype{10});
        QVERIFY(first.lineEnd);
        const auto middle = selection.columnsForRow(2, 0).value_or(lv::ui::SelectedColumns{});
        QVERIFY(middle.lineEnd);  // an empty middle row still selects its line break
        const auto last = selection.columnsForRow(3, 10).value_or(lv::ui::SelectedColumns{});
        QCOMPARE(last.from, qsizetype{0});
        QCOMPARE(last.to, qsizetype{4});
        QVERIFY(!last.lineEnd);
        QVERIFY(
            TextSelection::characters({.row = 1, .column = 2}, {.row = 1, .column = 2}).isEmpty());
        QVERIFY(!TextSelection::lines(5, 5).isEmpty());
    }

    static void dragSelectsAcrossLines()
    {
        const Fixture f({QStringLiteral("first line"), QStringLiteral("second line"),
                         QStringLiteral("third line")});
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, f.at(0, 6));
        QTest::mouseMove(f.view.viewport(), f.at(1, 3));
        QTest::mouseMove(f.view.viewport(), f.at(2, 5));
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, f.at(2, 5));
        QCOMPARE(f.view.selectedText(), QStringLiteral("line\nsecond line\nthird"));
        QCOMPARE(f.view.currentRow().value_or(LineNumber(99)).get(), std::uint64_t{2});

        // A plain click clears the selection.
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, f.at(1, 1));
        QVERIFY(f.view.selection().isEmpty());
        QVERIFY(f.view.selectedText().isEmpty());
    }

    static void dragBackwardsSelectsTheSameText()
    {
        const Fixture f({QStringLiteral("alpha beta"), QStringLiteral("gamma delta")});
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, f.at(1, 5));
        QTest::mouseMove(f.view.viewport(), f.at(0, 6));
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, f.at(0, 6));
        QCOMPARE(f.view.selectedText(), QStringLiteral("beta\ngamma"));
    }

    static void shiftClickExtendsTheSelection()
    {
        const Fixture f({QStringLiteral("one two"), QStringLiteral("three four")});
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, f.at(0, 4));
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, Qt::ShiftModifier, f.at(1, 5));
        QCOMPARE(f.view.selectedText(), QStringLiteral("two\nthree"));
    }

    static void doubleClickSelectsAWordAndTripleClickTheLine()
    {
        const Fixture f(
            {QStringLiteral("2026-01-01 ERROR [main] com.example_pkg.Service - failed")});
        const QPoint word = f.at(0, 30);  // inside "example_pkg"
        QTest::mouseDClick(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, word);
        QCOMPARE(f.view.selectedText(), QStringLiteral("example_pkg"));
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, word);
        QVERIFY(f.view.selection().lineMode);
        QCOMPARE(f.view.selectedText(),
                 QStringLiteral("2026-01-01 ERROR [main] com.example_pkg.Service - failed\n"));

        // A double click on punctuation selects only that character.
        QTest::mouseDClick(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, f.at(0, 17));
        QCOMPARE(f.view.selectedText(), QStringLiteral("["));
    }

    static void shiftArrowsSelectWholeLines()
    {
        Fixture f(
            {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
        QTest::keyClick(&f.view, Qt::Key_Down);
        QTest::keyClick(&f.view, Qt::Key_Down, Qt::ShiftModifier);
        QTest::keyClick(&f.view, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(f.view.selectedText(), QStringLiteral("b\nc\nd\n"));
        QTest::keyClick(&f.view, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(f.view.selectedText(), QStringLiteral("b\nc\n"));
        QTest::keyClick(&f.view, Qt::Key_Up, Qt::ShiftModifier);
        QTest::keyClick(&f.view, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(f.view.selectedText(), QStringLiteral("a\nb\n"));
        QTest::keyClick(&f.view, Qt::Key_Down);  // without Shift: selection is cleared
        QVERIFY(f.view.selection().isEmpty());
    }

    static void copyKeepsUmlautsAndTabs()
    {
        Fixture f({kUmlauts, QStringLiteral("x\ty"), QStringLiteral("last")});
        const qsizetype start = kUmlauts.indexOf(u'Ü');
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, f.at(0, start));
        QTest::mouseMove(f.view.viewport(), f.at(1, 2));
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, Qt::NoModifier, f.at(1, 2));
        const QString expected = kUmlauts.mid(start) + QStringLiteral("\nx\t");
        QCOMPARE(f.view.selectedText(), expected);

        QApplication::clipboard()->clear();
        QTest::keyClick(&f.view, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), expected);
    }

    static void clickPositionsRespectTabsAndSurrogates()
    {
        const Fixture f({kUmlauts});
        // Every column boundary maps back to itself (tabs expanded, the emoji is two code units).
        for (qsizetype column = 0; column <= kUmlauts.size(); ++column) {
            if (column < kUmlauts.size() && kUmlauts[column].isLowSurrogate()) {
                continue;
            }
            const TextPosition position = f.view.positionAt(f.at(0, column));
            QCOMPARE(position.column, column);
        }
    }

    static void selectAllCopiesEverything()
    {
        Fixture f({QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("partial")}, true);
        QTest::keyClick(&f.view, Qt::Key_A, Qt::ControlModifier);
        // The partial last line has no line feed in the file, so none is added.
        QCOMPARE(f.view.selectedText(), QStringLiteral("one\ntwo\npartial"));
        auto* action = f.view.findChild<QAction*>(QStringLiteral("actionSelectAll"));
        QVERIFY(action != nullptr);
        QCOMPARE(action->shortcut(), QKeySequence(QKeySequence::SelectAll));
    }

    static void selectAllAndCopyOfARealFileEqualsTheFile()
    {
        const lv::test::TempDir dir;
        lv::loggen::WriteOptions write;
        write.sizeBytes = std::uint64_t{5} * 1024 * 1024;
        write.out = dir / "gen.log";
        QVERIFY(lv::loggen::writeLog(write).ok());
        // Add CRLF lines, a tab, and umlauts so the comparison covers them.
        {
            lv::test::LogWriter writer(write.out);
            writer.append("crlf line\r\nGr\xC3\xB6\xC3\x9F"
                          "e\tTab\r\nend without newline");
        }

        lv::model::Document document(write.out);
        lv::model::DocumentLineSource source(document);
        lv::ui::LogView view;
        view.setLineSource(&source);
        document.open();
        QElapsedTimer timer;
        timer.start();
        while (document.state() != lv::model::Document::State::kReady && timer.elapsed() < 10000) {
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        QCOMPARE(document.state(), lv::model::Document::State::kReady);

        view.selectAll();
        const QString copied = view.selectedText();

        QFile file(QString::fromStdString(write.out.string()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString expected = QString::fromUtf8(file.readAll());
        expected.remove(u'\r');
        QCOMPARE(copied.size(), expected.size());
        QVERIFY(copied == expected);
    }

    static void resetClearsTheSelection()
    {
        lv::test::FakeLineSource source(100);
        lv::ui::LogView view;
        view.setLineSource(&source);
        view.selectAll();
        QVERIFY(!view.selection().isEmpty());
        source.setRows(10, true);
        QVERIFY(view.selection().isEmpty());
    }
};

QTEST_MAIN(LogViewSelectionTest)
#include "log_view_selection_test.moc"
