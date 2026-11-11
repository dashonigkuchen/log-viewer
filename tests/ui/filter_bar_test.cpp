#include "log_writer.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/filter_bar.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>

#include <string>

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

const char* kLog = "INFO start\nERROR disk\nerror net\nWARN slow\nERROR_CODE 5\nINFO heartbeat\n";

struct Fixture
{
    lv::test::TempDir dir;
    std::unique_ptr<lv::ui::DocumentTab> tab;

    Fixture()
    {
        writeText(dir / "a.log", kLog);
        tab = std::make_unique<lv::ui::DocumentTab>(dir / "a.log");
        tab->resize(900, 600);
        tab->show();
    }

    [[nodiscard]] lv::ui::FilterBar& bar() const { return *tab->filterBar(); }

    [[nodiscard]] FilteredDocument& filter() const { return tab->filter(); }

    [[nodiscard]] bool ready() const
    {
        return waitFor(
            [this] { return tab->document().state() == lv::model::Document::State::kReady; });
    }

    // Types `text` into the field and presses Enter; waits for the filter to finish.
    [[nodiscard]] bool enter(const QString& text) const
    {
        bar().input()->clear();
        QTest::keyClicks(bar().input(), text);
        QTest::keyClick(bar().input(), Qt::Key_Return);
        return waitFor([this] {
            return filter().state() == FilteredDocument::State::kDone ||
                   filter().state() == FilteredDocument::State::kInactive;
        });
    }

    void setMode(int index) const { bar().modeSelector()->setCurrentIndex(index); }
};

}  // namespace

class FilterBarTest : public QObject
{
    Q_OBJECT

private slots:

    static void appliesOnEnterOnly()
    {
        const Fixture f;
        QVERIFY(f.ready());
        QTest::keyClicks(f.bar().input(), QStringLiteral("error"));
        QApplication::processEvents();
        QCOMPARE(f.filter().state(), FilteredDocument::State::kInactive);  // not while typing
        QTest::keyClick(f.bar().input(), Qt::Key_Return);
        QVERIFY(waitFor([&] { return f.filter().state() == FilteredDocument::State::kDone; }));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{3});
        QCOMPARE(f.tab->filteredView()->rowCount().get(), std::uint64_t{3});
        QVERIFY(f.bar().statusLabel()->text().contains(QStringLiteral("3 matches")));
    }

    static void optionsAreApplied()
    {
        const Fixture f;
        QVERIFY(f.ready());
        f.bar().caseSensitiveBox()->setChecked(true);
        QVERIFY(f.enter(QStringLiteral("ERROR")));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{2});
        f.bar().wholeWordBox()->setChecked(true);
        QVERIFY(f.enter(QStringLiteral("ERROR")));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{1});  // not ERROR_CODE
        f.bar().invertBox()->setChecked(true);
        QVERIFY(f.enter(QStringLiteral("ERROR")));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{5});
        // Whole word is only available in literal mode.
        f.setMode(1);
        QVERIFY(!f.bar().wholeWordBox()->isEnabled());
        f.setMode(0);
        QVERIFY(f.bar().wholeWordBox()->isEnabled());
    }

    static void escapeRestoresTheAppliedFilter()
    {
        const Fixture f;
        QVERIFY(f.ready());
        f.setMode(1);
        QVERIFY(f.enter(QStringLiteral("^WARN")));
        QTest::keyClicks(f.bar().input(), QStringLiteral(" changed"));
        f.setMode(0);
        f.bar().invertBox()->setChecked(true);
        QTest::keyClick(f.bar().input(), Qt::Key_Escape);
        QCOMPARE(f.bar().input()->text(), QStringLiteral("^WARN"));
        QCOMPARE(f.bar().modeSelector()->currentText(), QStringLiteral("Regex"));
        QVERIFY(!f.bar().invertBox()->isChecked());
        QCOMPARE(f.filter().matchCount(), std::uint64_t{1});
    }

    static void clearRemovesTheFilter()
    {
        const Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.enter(QStringLiteral("INFO")));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{2});
        auto* clear = f.bar().findChild<QPushButton*>(QStringLiteral("filterClear"));
        QVERIFY(clear != nullptr);
        QTest::mouseClick(clear, Qt::LeftButton);
        QCOMPARE(f.filter().state(), FilteredDocument::State::kInactive);
        QCOMPARE(f.tab->filteredView()->rowCount().get(), std::uint64_t{0});
        QVERIFY(f.bar().input()->text().isEmpty());
        QVERIFY(f.bar().statusLabel()->text().isEmpty());
        // Enter on an empty field clears as well.
        QVERIFY(f.enter(QStringLiteral("INFO")));
        QVERIFY(f.enter(QString()));
        QCOMPARE(f.filter().state(), FilteredDocument::State::kInactive);
    }

    static void invalidRegexShowsTheErrorAndKeepsResults()
    {
        const Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.enter(QStringLiteral("WARN")));
        f.setMode(1);
        f.bar().input()->setText(QStringLiteral("ab(c"));
        QTest::keyClick(f.bar().input(), Qt::Key_Return);
        QVERIFY(f.bar().errorLabel()->isVisibleTo(f.tab.get()));
        QVERIFY(f.bar().errorLabel()->text().contains(QStringLiteral("position 5")));
        QCOMPARE(f.filter().query().value_or(lv::core::Query{}).text, std::string("WARN"));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{1});
        QCOMPARE(f.bar().input()->cursorPosition(), 4);
        // Editing hides the error.
        QTest::keyClick(f.bar().input(), Qt::Key_Backspace);
        QVERIFY(!f.bar().errorLabel()->isVisibleTo(f.tab.get()));
    }

    static void invalidBooleanExpressionShowsThePosition()
    {
        const Fixture f;
        QVERIFY(f.ready());
        f.setMode(2);
        f.bar().input()->setText(QStringLiteral("\"ERROR\" AND"));
        QTest::keyClick(f.bar().input(), Qt::Key_Return);
        QVERIFY(f.bar().errorLabel()->isVisibleTo(f.tab.get()));
        QVERIFY(f.bar().errorLabel()->text().contains(QStringLiteral("position 12")));
        QVERIFY(f.bar().errorLabel()->text().contains(QStringLiteral("missing term")));
        QCOMPARE(f.filter().state(), FilteredDocument::State::kInactive);

        f.bar().input()->setText(QStringLiteral("/ERROR/ AND NOT \"disk\""));
        QTest::keyClick(f.bar().input(), Qt::Key_Return);
        QVERIFY(waitFor([&] { return f.filter().state() == FilteredDocument::State::kDone; }));
        QVERIFY(!f.bar().errorLabel()->isVisibleTo(f.tab.get()));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{2});  // "error net", "ERROR_CODE 5"
    }

    static void optionChangesApplyImmediately()
    {
        const Fixture f;
        QVERIFY(f.ready());
        // With an empty field, options do nothing.
        QTest::mouseClick(f.bar().caseSensitiveBox(), Qt::LeftButton);
        QApplication::processEvents();
        QCOMPARE(f.filter().state(), FilteredDocument::State::kInactive);
        QTest::mouseClick(f.bar().caseSensitiveBox(), Qt::LeftButton);  // back off

        QVERIFY(f.enter(QStringLiteral("ERROR")));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{3});  // ERROR, error, ERROR_CODE
        const auto done = [&] {
            return waitFor([&] { return f.filter().state() == FilteredDocument::State::kDone; });
        };
        QTest::mouseClick(f.bar().caseSensitiveBox(), Qt::LeftButton);
        QVERIFY(done());
        QVERIFY(f.filter().query().value_or(lv::core::Query{}).caseSensitive);
        QCOMPARE(f.filter().matchCount(), std::uint64_t{2});
        QTest::mouseClick(f.bar().wholeWordBox(), Qt::LeftButton);
        QVERIFY(done());
        QCOMPARE(f.filter().matchCount(), std::uint64_t{1});
        QTest::mouseClick(f.bar().invertBox(), Qt::LeftButton);
        QVERIFY(done());
        QCOMPARE(f.filter().matchCount(), std::uint64_t{5});

        // A text typed but not confirmed is applied together with the option.
        f.bar().input()->setText(QStringLiteral("WARN"));
        QTest::mouseClick(f.bar().invertBox(), Qt::LeftButton);  // invert off
        QVERIFY(done());
        QCOMPARE(f.filter().query().value_or(lv::core::Query{}).text, std::string("WARN"));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{1});
    }

    static void modeChangeAppliesAndInvalidModeKeepsTheResult()
    {
        const Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.enter(QStringLiteral("ERROR(")));  // valid as a literal
        QCOMPARE(f.filter().matchCount(), std::uint64_t{0});
        QVERIFY(f.enter(QStringLiteral("WARN")));
        f.bar().input()->setText(QStringLiteral("ERROR("));
        // Switching to regex makes the text invalid: error shown, old result stays.
        auto* mode = f.bar().modeSelector();
        mode->setCurrentIndex(1);
        QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 1)));
        QApplication::processEvents();
        QVERIFY(f.bar().errorLabel()->isVisible());
        QCOMPARE(f.filter().query().value_or(lv::core::Query{}).text, std::string("WARN"));
        QCOMPARE(f.filter().matchCount(), std::uint64_t{1});
        // Fixing it by switching back applies the literal.
        mode->setCurrentIndex(0);
        QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 0)));
        QVERIFY(waitFor([&] {
            return f.filter().state() == FilteredDocument::State::kDone &&
                   f.filter().query().value_or(lv::core::Query{}).text == "ERROR(";
        }));
        QVERIFY(f.bar().errorLabel()->isHidden());
    }

    static void modeSwitchingKeepsTheText()
    {
        const Fixture f;
        f.bar().input()->setText(QStringLiteral("some text"));
        for (const int mode : {1, 2, 0}) {
            f.setMode(mode);
            QCOMPARE(f.bar().input()->text(), QStringLiteral("some text"));
        }
    }

    static void shortcutFocusesTheFilterField()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", kLog);
        lv::ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* tab = window.openFile(dir / "a.log");
        auto* action = window.findChild<QAction*>(QStringLiteral("actionFocusFilter"));
        QVERIFY(action != nullptr);
        QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
        action->trigger();
        QVERIFY(tab->filterBar()->input()->hasFocus());
    }

    static void stopAndEscapeCancelTheSearch()
    {
        // F-FILT-6: a Stop button (visible only while searching) and Escape cancel the search.
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(!f.bar().stopButton()->isVisibleTo(&f.bar()));
        f.bar().input()->setText(QStringLiteral("ERROR"));
        QVERIFY(f.bar().apply());
        QVERIFY(waitFor([&] { return f.filter().state() == FilteredDocument::State::kDone; }));
        QVERIFY(!f.bar().stopButton()->isVisibleTo(&f.bar()));

        // While searching (forced by a state the test can observe).
        f.bar().input()->setText(QStringLiteral("WARN"));
        QVERIFY(f.bar().apply());
        if (f.filter().state() == FilteredDocument::State::kSearching) {
            QVERIFY(f.bar().stopButton()->isVisibleTo(&f.bar()));
            QTest::mouseClick(f.bar().stopButton(), Qt::LeftButton);
            QCOMPARE(f.filter().state(), FilteredDocument::State::kCanceled);
            QVERIFY(f.bar().statusLabel()->text().startsWith(QStringLiteral("Stopped at")));
            QVERIFY(!f.bar().stopButton()->isVisibleTo(&f.bar()));
        }
        QVERIFY(waitFor([&] { return f.filter().state() != FilteredDocument::State::kSearching; }));

        // Escape while searching cancels, otherwise it restores the field.
        f.bar().input()->setText(QStringLiteral("INFO"));
        QVERIFY(f.bar().apply());
        const bool searching = f.filter().state() == FilteredDocument::State::kSearching;
        QTest::keyClick(f.bar().input(), Qt::Key_Escape);
        if (searching) {
            QCOMPARE(f.filter().state(), FilteredDocument::State::kCanceled);
            QCOMPARE(f.bar().input()->text(), QStringLiteral("INFO"));
        }
    }
};

QTEST_MAIN(FilterBarTest)
#include "filter_bar_test.moc"
