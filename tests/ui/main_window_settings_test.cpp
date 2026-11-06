#include "log_writer.h"
#include "model/settings.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"

#include <QAction>
#include <QTest>

namespace
{

void writeText(const std::filesystem::path& path, const std::string& text)
{
    lv::test::LogWriter writer(path, true);
    writer.append(text);
}

QAction* action(const lv::ui::MainWindow& window, const char* name)
{
    return window.findChild<QAction*>(QString::fromLatin1(name));
}

}  // namespace

class MainWindowSettingsTest : public QObject
{
    Q_OBJECT

private slots:

    static void zoomShortcutsChangeTheViewFont()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "x\n");
        lv::model::Settings settings(QString::fromStdString((dir / "settings").string()));
        lv::ui::MainWindow window;
        window.setSettings(&settings);
        auto* tab = window.openFile(dir / "a.log");
        const int base = tab->mainView()->font().pointSize();
        QCOMPARE(base, settings.fontSize());

        QCOMPARE(action(window, "actionZoomIn")->shortcut(), QKeySequence(QKeySequence::ZoomIn));
        QCOMPARE(action(window, "actionZoomReset")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_0));
        action(window, "actionZoomIn")->trigger();
        action(window, "actionZoomIn")->trigger();
        QCOMPARE(tab->mainView()->font().pointSize(), base + 2);
        QCOMPARE(settings.zoom(), 2);
        action(window, "actionZoomOut")->trigger();
        QCOMPARE(tab->mainView()->font().pointSize(), base + 1);
        action(window, "actionZoomReset")->trigger();
        QCOMPARE(tab->mainView()->font().pointSize(), base);
    }

    static void settingsApplyToNewAndExistingTabs()
    {
        const lv::test::TempDir dir;
        writeText(dir / "a.log", "x\n");
        writeText(dir / "b.log", "y\n");
        lv::model::Settings settings(QString::fromStdString((dir / "settings").string()));
        settings.setTabWidth(4);
        lv::ui::MainWindow window;
        window.setSettings(&settings);
        auto* a = window.openFile(dir / "a.log");
        QCOMPARE(a->mainView()->tabWidth(), 4);

        action(window, "actionLineNumbers")->setChecked(false);
        QVERIFY(!settings.lineNumbersVisible());
        QVERIFY(!a->mainView()->lineNumbersVisible());
        auto* b = window.openFile(dir / "b.log");
        QVERIFY(!b->mainView()->lineNumbersVisible());

        settings.setTabWidth(2);
        QCOMPARE(a->mainView()->tabWidth(), 2);
        QCOMPARE(b->mainView()->tabWidth(), 2);
    }
};

QTEST_MAIN(MainWindowSettingsTest)
#include "main_window_settings_test.moc"
