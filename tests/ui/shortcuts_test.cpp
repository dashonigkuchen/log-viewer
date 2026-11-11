#include "log_writer.h"
#include "model/document.h"
#include "model/mark_set.h"
#include "model/settings.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"
#include "ui/shortcuts.h"

#include <QAction>
#include <QLabel>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>

using lv::ui::MainWindow;
using lv::ui::ShortcutsDialog;

namespace
{

QAction* actionNamed(const MainWindow& window, const QString& name)
{
    const auto& actions = window.shortcutActions();
    const auto it = std::ranges::find_if(
        actions, [&name](const QAction* action) { return action->objectName() == name; });
    return it != actions.end() ? *it : nullptr;
}

QString settingsDir(const lv::test::TempDir& dir)
{
    return QString::fromStdU16String((dir / "settings").u16string());
}

}  // namespace

class ShortcutsTest : public QObject
{
    Q_OBJECT

private slots:

    static void conflictsAreFound()
    {
        const QKeySequence f5(Qt::Key_F5);
        const QKeySequence f6(Qt::Key_F6);
        const auto conflicts = lv::ui::findShortcutConflicts({{QStringLiteral("b"), {f5}},
                                                              {QStringLiteral("a"), {f5, f6}},
                                                              {QStringLiteral("c"), {}}});
        QCOMPARE(conflicts.size(), std::size_t{1});
        QCOMPARE(conflicts[0].key, f5);
        QCOMPARE(conflicts[0].names, QStringList({QStringLiteral("a"), QStringLiteral("b")}));
        QVERIFY(lv::ui::findShortcutConflicts({{QStringLiteral("a"), {f5}}}).empty());
    }

    static void defaultActionsHaveNoConflicts()
    {
        const MainWindow window;
        QVERIFY(window.shortcutActions().size() > 20);
        QVERIFY(actionNamed(window, QStringLiteral("actionReload")) != nullptr);
        QVERIFY(actionNamed(window, QStringLiteral("actionToggleBookmark")) != nullptr);
        QVERIFY(actionNamed(window, QStringLiteral("actionBookmarksPanel")) != nullptr);
        const ShortcutsDialog dialog(window.shortcutActions(), {});
        QVERIFY2(dialog.conflicts().empty(), qPrintable(dialog.conflictLabel()->text()));
        QCOMPARE(dialog.list()->topLevelItemCount(),
                 static_cast<int>(window.shortcutActions().size()));
        QCOMPARE(lv::ui::actionLabel(*actionNamed(window, QStringLiteral("actionOpen"))),
                 QStringLiteral("Open"));
    }

    static void rebindingDetectsConflictsAndPersists()
    {
        const lv::test::TempDir dir;
        {
            lv::model::Settings settings(settingsDir(dir));
            MainWindow window;
            window.setSettings(&settings);
            QAction* reload = actionNamed(window, QStringLiteral("actionReload"));
            QAction* follow = actionNamed(window, QStringLiteral("actionFollow"));
            QCOMPARE(reload->shortcut(), QKeySequence(Qt::Key_F5));

            // A conflicting binding blocks OK; the dialog stays open.
            ShortcutsDialog conflicting(window.shortcutActions(), settings.shortcuts());
            conflicting.setShortcut(QStringLiteral("actionReload"), QKeySequence(Qt::Key_F10));
            QCOMPARE(conflicting.conflicts().size(), std::size_t{1});
            QVERIFY(conflicting.conflictLabel()->text().contains(QStringLiteral("Follow")));
            QTimer::singleShot(0, [] {
                if (auto* box = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                    box->reject();  // the warning
                }
            });
            conflicting.accept();
            QCOMPARE(conflicting.result(), int(QDialog::Rejected));

            // Rebind Reload to F6 and remove Follow's shortcut through the main window.
            window.editShortcuts([](ShortcutsDialog& dialog) {
                dialog.setShortcut(QStringLiteral("actionReload"), QKeySequence(Qt::Key_F6));
                dialog.setShortcut(QStringLiteral("actionFollow"), QKeySequence());
                QVERIFY(dialog.conflicts().empty());
                QTimer::singleShot(0, &dialog, &QDialog::accept);
            });
            QCOMPARE(reload->shortcut(), QKeySequence(Qt::Key_F6));
            QVERIFY(follow->shortcut().isEmpty());
        }
        // A new start applies the stored shortcuts.
        lv::model::Settings settings(settingsDir(dir));
        QCOMPARE(settings.shortcuts().size(), std::size_t{2});
        MainWindow window;
        window.setSettings(&settings);
        QAction* reload = actionNamed(window, QStringLiteral("actionReload"));
        QCOMPARE(reload->shortcut(), QKeySequence(Qt::Key_F6));
        QVERIFY(actionNamed(window, QStringLiteral("actionFollow"))->shortcut().isEmpty());

        // Reset All restores the defaults.
        window.editShortcuts([](ShortcutsDialog& dialog) {
            dialog.resetAll();
            QTimer::singleShot(0, &dialog, &QDialog::accept);
        });
        QVERIFY(settings.shortcuts().empty());
        QCOMPARE(reload->shortcut(), QKeySequence(Qt::Key_F5));
        QCOMPARE(actionNamed(window, QStringLiteral("actionFollow"))->shortcut(),
                 QKeySequence(Qt::Key_F10));
    }

    static void settingBackTheDefaultDropsTheOverride()
    {
        const MainWindow window;
        ShortcutsDialog dialog(window.shortcutActions(), {});
        dialog.setShortcut(QStringLiteral("actionReload"), QKeySequence(Qt::Key_F6));
        QCOMPARE(dialog.overrides().size(), std::size_t{1});
        dialog.setShortcut(QStringLiteral("actionReload"), QKeySequence(Qt::Key_F5));
        QVERIFY(dialog.overrides().empty());
    }

    static void markNavigationIsConfigurable()
    {
        // F-MARK-3: Ctrl+Down/Up by default, rebindable like every menu command.
        const lv::test::TempDir dir;
        {
            lv::test::LogWriter writer(dir / "m.log", true);
            writer.append("a\nb\nc\nd\ne\nf\n");
        }
        lv::model::Settings settings(settingsDir(dir));
        MainWindow window;
        window.setSettings(&settings);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QAction* next = actionNamed(window, QStringLiteral("actionNextMark"));
        QAction* previous = actionNamed(window, QStringLiteral("actionPreviousMark"));
        QVERIFY(next != nullptr && previous != nullptr);
        QCOMPARE(next->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Down));
        QCOMPARE(previous->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Up));

        auto* tab = window.openFile(dir / "m.log");
        QVERIFY(QTest::qWaitFor(
            [tab] { return tab->document().state() == lv::model::Document::State::kReady; }));
        tab->marks().toggle(std::vector{lv::core::LineNumber(2), lv::core::LineNumber(4)});
        auto* view = tab->mainView();
        view->setFocus();
        view->setCurrentRow(lv::core::LineNumber(0));
        QTest::keyClick(view, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(view->currentRow().value_or(lv::core::LineNumber(99)).get(), std::uint64_t{2});

        window.editShortcuts([](ShortcutsDialog& dialog) {
            dialog.setShortcut(QStringLiteral("actionNextMark"), QKeySequence(Qt::Key_F7));
            QTimer::singleShot(0, &dialog, &QDialog::accept);
        });
        QCOMPARE(next->shortcut(), QKeySequence(Qt::Key_F7));
        // The modal dialog took the activation; window shortcuts need the window active again.
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        view->setFocus();
        QTest::keyClick(view, Qt::Key_F7);
        QCOMPARE(view->currentRow().value_or(lv::core::LineNumber(99)).get(), std::uint64_t{4});
    }
};

QTEST_MAIN(ShortcutsTest)
#include "shortcuts_test.moc"
