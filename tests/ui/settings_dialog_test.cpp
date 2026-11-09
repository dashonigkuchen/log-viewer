#include "log_writer.h"
#include "model/document.h"
#include "model/file_watcher.h"
#include "model/filter_history.h"
#include "model/settings.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"
#include "ui/settings_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QTest>
#include <QTimer>

using lv::model::Settings;
using lv::ui::SettingsDialog;

namespace
{

QString qpath(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

}  // namespace

class SettingsDialogTest : public QObject
{
    Q_OBJECT

private slots:

    static void everySettingTakesEffectAndPersists()
    {
        const lv::test::TempDir dir;
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append(std::string(300, 'x') + "\n");
        }
        Settings settings(qpath(dir / "settings"));
        lv::model::FilterHistory history;
        lv::ui::MainWindow window;
        window.setFilterHistory(&history);
        window.setSettings(&settings);
        auto* tab = window.openFile(dir / "a.log");
        QVERIFY(tab != nullptr);
        window.editSettings([](SettingsDialog& dialog) {
            dialog.fontSizeBox()->setValue(14);
            dialog.tabWidthBox()->setValue(3);
            dialog.lineNumbersBox()->setChecked(false);
            dialog.columnsBox()->setValue(200);
            dialog.currentLineEdit()->setText(QStringLiteral("#FF101010"));
            dialog.markEdit()->setText(QStringLiteral("#80FF0000"));
            dialog.matchEdit()->setText(QStringLiteral("#00FF00"));
            dialog.pollBox()->setValue(2000);
            dialog.historyBox()->setValue(5);
            dialog.sessionBox()->setChecked(false);
            dialog.singleInstanceBox()->setChecked(false);
            dialog.threadsBox()->setValue(2);
            QTimer::singleShot(0, &dialog, &QDialog::accept);
        });
        // Effect on the open tab.
        auto* view = tab->mainView();
        QCOMPARE(view->font().pointSize(), 14);
        QCOMPARE(view->tabWidth(), 3);
        QVERIFY(!view->lineNumbersVisible());
        QCOMPARE(view->columnLimit(), 200);
        QCOMPARE(view->currentLineColor(), QColor::fromRgba(0xFF101010U));
        QCOMPARE(view->markColor(), QColor::fromRgba(0x80FF0000U));
        QCOMPARE(view->matchHighlightColor(), QColor::fromRgba(0xFF00FF00U));
        QCOMPARE(tab->filteredView()->columnLimit(), 200);
        QCOMPARE(history.limit(), 5);
        QVERIFY(tab->document().watcher() == nullptr ||
                tab->document().watcher()->pollInterval() == 2000);
        // Persistence.
        const Settings loaded(qpath(dir / "settings"));
        QCOMPARE(loaded.fontSize(), 14);
        QCOMPARE(loaded.tabWidth(), 3);
        QVERIFY(!loaded.lineNumbersVisible());
        QCOMPARE(loaded.maxColumns(), 200);
        QCOMPARE(loaded.currentLineColor().value_or(0), 0xFF101010U);
        QCOMPARE(loaded.markColor(), 0x80FF0000U);
        QCOMPARE(loaded.matchColor(), 0xFF00FF00U);
        QCOMPARE(loaded.pollInterval(), 2000);
        QCOMPARE(loaded.filterHistorySize(), 5);
        QVERIFY(!loaded.restoreSession());
        QVERIFY(!loaded.singleInstance());
        QCOMPARE(loaded.searchThreads(), 2);
    }

    static void cancelAndInvalidColorsChangeNothing()
    {
        const lv::test::TempDir dir;
        Settings settings(qpath(dir.path()));
        {
            SettingsDialog dialog(settings);
            dialog.tabWidthBox()->setValue(2);
            dialog.reject();
        }
        QCOMPARE(settings.tabWidth(), Settings::kDefaultTabWidth);
        SettingsDialog dialog(settings);
        dialog.tabWidthBox()->setValue(2);
        dialog.markEdit()->setText(QStringLiteral("red"));
        // apply() shows a warning box; close it automatically.
        QTimer::singleShot(0, [] {
            if (QWidget* box = QApplication::activeModalWidget()) {
                box->close();
            }
        });
        QVERIFY(!dialog.apply());
        QCOMPARE(settings.tabWidth(), Settings::kDefaultTabWidth);
        // Empty current-line color = from the theme.
        dialog.markEdit()->setText(QStringLiteral("#80FF0000"));
        dialog.currentLineEdit()->clear();
        QVERIFY(dialog.apply());
        QVERIFY(!settings.currentLineColor().has_value());
    }

    static void corruptSettingsShowANotice()
    {
        const lv::test::TempDir dir;
        {
            QFile file(qpath(dir / "logviewer.ini"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[View\n");
        }
        Settings settings(qpath(dir.path()));
        lv::ui::MainWindow window;
        window.setSettings(&settings);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("reset")));
    }
};

QTEST_MAIN(SettingsDialogTest)
#include "settings_dialog_test.moc"
