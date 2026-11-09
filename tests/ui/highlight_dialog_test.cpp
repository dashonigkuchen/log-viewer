#include "log_writer.h"
#include "model/highlight_sets.h"
#include "model/line_highlighter.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/highlight_dialog.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTest>
#include <QTimer>

using lv::model::HighlightScope;
using lv::model::HighlightSet;
using lv::model::HighlightSets;
using lv::ui::HighlightDialog;

namespace
{

QStringList previewLines()
{
    return {QStringLiteral("INFO start"), QStringLiteral("ERROR disk full"),
            QStringLiteral("INFO user=42")};
}

// Whether preview row `row` has any pixel of `color` (ignoring alpha).
bool rowHasColor(lv::ui::LogView* view, int row, QRgb color)
{
    QImage image(view->viewport()->size(), QImage::Format_ARGB32);
    view->viewport()->render(&image);
    const int top = row * view->lineHeight();
    for (int y = top; y < top + view->lineHeight() && y < image.height(); ++y) {
        for (int x = view->gutterWidth(); x < image.width(); ++x) {
            if ((image.pixel(x, y) & 0xFFFFFFU) == (color & 0xFFFFFFU)) {
                return true;
            }
        }
    }
    return false;
}

void click(const HighlightDialog& dialog, const char* name)
{
    auto* b = dialog.findChild<QPushButton*>(QString::fromLatin1(name));
    QVERIFY(b != nullptr);
    b->click();
}

}  // namespace

class HighlightDialogTest : public QObject
{
    Q_OBJECT

private slots:

    static void createAndEditRulesWithLivePreview()
    {
        HighlightSets sets;
        sets.assign({HighlightSet{.name = QStringLiteral("Empty"), .rules = {}}}, 0);
        HighlightDialog dialog(sets, previewLines());
        dialog.resize(900, 600);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        click(dialog, "addRule");
        QCOMPARE(dialog.ruleList()->count(), 1);
        dialog.patternEdit()->clear();
        QTest::keyClicks(dialog.patternEdit(), QStringLiteral("ERROR"));
        dialog.backgroundEdit()->clear();
        QTest::keyClicks(dialog.backgroundEdit(), QStringLiteral("#20C020"));
        const auto& rule = dialog.sets()[0].rules[0];
        QCOMPARE(rule.pattern, QStringLiteral("ERROR"));
        QCOMPARE(rule.background.value_or(0), 0xFF20C020U);
        // Live preview: row 1 ("ERROR disk full") is green, row 0 is not.
        QVERIFY(rowHasColor(dialog.preview(), 1, 0xFF20C020U));
        QVERIFY(!rowHasColor(dialog.preview(), 0, 0xFF20C020U));

        // Match-only scope, regex, invalid input shows an error.
        dialog.scopeBox()->setCurrentIndex(1);
        dialog.regexBox()->setChecked(true);
        dialog.patternEdit()->clear();
        QTest::keyClicks(dialog.patternEdit(), QStringLiteral("("));
        QVERIFY(dialog.errorLabel()->text().contains(QStringLiteral("Invalid pattern")));
        dialog.patternEdit()->clear();
        QTest::keyClicks(dialog.patternEdit(), QStringLiteral("user=\\d+"));
        QVERIFY(dialog.errorLabel()->text().isEmpty());
        QTest::keyClicks(dialog.backgroundEdit(), QStringLiteral("x"));
        QVERIFY(dialog.errorLabel()->text().contains(QStringLiteral("background")));
        dialog.backgroundEdit()->clear();
        QTest::keyClicks(dialog.backgroundEdit(), QStringLiteral("#2020D0"));
        QCOMPARE(dialog.sets()[0].rules[0].scope, HighlightScope::kMatchOnly);
        QVERIFY(rowHasColor(dialog.preview(), 2, 0xFF2020D0U));

        // Disable via the check box: preview updates.
        dialog.ruleList()->item(0)->setCheckState(Qt::Unchecked);
        QVERIFY(!dialog.sets()[0].rules[0].enabled);
        QVERIFY(!rowHasColor(dialog.preview(), 2, 0xFF2020D0U));
    }

    static void reorderDuplicateAndDeleteRules()
    {
        HighlightSets sets;
        HighlightSet set{.name = QStringLiteral("S"), .rules = {}};
        for (const char* pattern : {"a", "b", "c"}) {
            set.rules.push_back({.pattern = QString::fromLatin1(pattern),
                                 .regex = false,
                                 .caseSensitive = false,
                                 .scope = HighlightScope::kWholeLine,
                                 .foreground = std::nullopt,
                                 .background = 0xFF000001U,
                                 .enabled = true});
        }
        sets.assign({set}, 0);
        HighlightDialog dialog(sets, previewLines());
        const auto patterns = [&dialog] {
            QStringList out;
            for (const auto& rule : dialog.sets()[0].rules) {
                out << rule.pattern;
            }
            return out.join(u',');
        };
        dialog.selectRule(2);
        click(dialog, "moveRuleUp");
        QCOMPARE(patterns(), QStringLiteral("a,c,b"));
        QCOMPARE(dialog.ruleList()->currentRow(), 1);
        click(dialog, "moveRuleDown");
        QCOMPARE(patterns(), QStringLiteral("a,b,c"));
        dialog.selectRule(0);
        click(dialog, "duplicateRule");
        QCOMPARE(patterns(), QStringLiteral("a,a,b,c"));
        QCOMPARE(dialog.ruleList()->currentRow(), 1);  // the copy
        click(dialog, "deleteRule");
        QCOMPARE(patterns(), QStringLiteral("a,b,c"));
        dialog.selectRule(0);
        click(dialog, "deleteRule");
        QCOMPARE(patterns(), QStringLiteral("b,c"));

        // Drag and drop: the list model's move is applied to the rules.
        QVERIFY(dialog.ruleList()->model()->moveRow({}, 1, {}, 0));
        QCOMPARE(patterns(), QStringLiteral("c,b"));
    }

    static void manageSetsAndApplyThroughTheMainWindow()
    {
        const lv::test::TempDir dir;
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append("INFO a\nERROR b\n");
        }
        lv::ui::MainWindow window;
        HighlightSets sets;
        window.setHighlightSets(&sets);
        auto* tab = window.openFile(dir / "a.log");
        QVERIFY(tab != nullptr);
        window.editHighlightRules([](HighlightDialog& dialog) {
            QCOMPARE(dialog.setList()->count(), 1);  // built-in default
            click(dialog, "duplicateSet");
            QCOMPARE(dialog.setList()->count(), 2);
            QCOMPARE(dialog.sets()[1].name, QStringLiteral("Default copy"));
            click(dialog, "newSet");
            QCOMPARE(dialog.sets()[2].name, QStringLiteral("New Set"));
            click(dialog, "makeDefault");
            QCOMPARE(dialog.defaultIndex(), std::size_t{2});
            // Rename in place.
            dialog.setList()->item(2)->setText(QStringLiteral("Mine"));
            QCOMPARE(dialog.sets()[2].name, QStringLiteral("Mine"));
            dialog.selectSet(1);
            click(dialog, "deleteSet");
            QCOMPARE(dialog.setList()->count(), 2);
            QCOMPARE(dialog.defaultIndex(), std::size_t{1});
            QTimer::singleShot(0, &dialog, &QDialog::accept);
        });
        QCOMPARE(sets.size(), std::size_t{2});
        QCOMPARE(sets.set(1).name, QStringLiteral("Mine"));
        QCOMPARE(sets.defaultIndex(), std::size_t{1});
        QCOMPARE(tab->lineHighlighter().activeSet().value_or(99), std::size_t{1});

        // Cancel discards edits.
        window.editHighlightRules([](HighlightDialog& dialog) {
            click(dialog, "newSet");
            QTimer::singleShot(0, &dialog, &QDialog::reject);
        });
        QCOMPARE(sets.size(), std::size_t{2});
    }

    static void importAndExportJson()
    {
        const lv::test::TempDir dir;
        const QString path = QString::fromStdString((dir / "sets.json").string());
        const HighlightSets sets;
        HighlightDialog dialog(sets, previewLines());
        dialog.setFileChoosers([&path] { return QString(path); },
                               [&path] { return QString(path); });
        click(dialog, "exportSets");
        QVERIFY(QFile::exists(path));
        click(dialog, "importSets");
        QCOMPARE(dialog.sets().size(), std::size_t{2});
        QCOMPARE(dialog.sets()[1].name, QStringLiteral("Default (2)"));
        QCOMPARE(dialog.sets()[1].rules, dialog.sets()[0].rules);
    }

    static void highlightThisTextAddsAMatchRule()
    {
        const lv::test::TempDir dir;
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append("INFO user=42 login\n");
        }
        lv::ui::MainWindow window;
        HighlightSets sets;
        window.setHighlightSets(&sets);
        auto* tab = window.openFile(dir / "a.log");
        QVERIFY(tab != nullptr);
        const std::size_t before = sets.set(0).rules.size();
        emit tab->mainView()->highlightTextRequested(QStringLiteral("user=42"));
        QCOMPARE(sets.set(0).rules.size(), before + 1);
        const auto& rule = sets.set(0).rules.back();
        QCOMPARE(rule.pattern, QStringLiteral("user=42"));
        QCOMPARE(rule.scope, HighlightScope::kMatchOnly);
        QVERIFY(!rule.regex);
    }

    static void colorParsing()
    {
        QCOMPARE(HighlightDialog::parseColor(QStringLiteral("#FF0000")).value_or(std::nullopt),
                 std::optional<lv::model::Argb>(0xFFFF0000U));
        QCOMPARE(HighlightDialog::parseColor(QStringLiteral(" ")).value_or(0xDEADU),
                 std::optional<lv::model::Argb>());
        QVERIFY(!HighlightDialog::parseColor(QStringLiteral("red")).has_value());
        QCOMPARE(HighlightDialog::formatColor(0xFF00AA11U), QStringLiteral("#00AA11"));
        QCOMPARE(HighlightDialog::formatColor(0x8000AA11U), QStringLiteral("#8000AA11"));
    }
};

QTEST_MAIN(HighlightDialogTest)
#include "highlight_dialog_test.moc"
