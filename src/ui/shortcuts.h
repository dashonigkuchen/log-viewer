#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

#include <map>
#include <vector>

class QAction;
class QKeySequenceEdit;
class QLabel;
class QTreeWidget;

namespace lv::ui
{

// Configurable keyboard shortcuts (SPEC F-SET-5).

// Remembers the action's current shortcuts as its defaults (once).
void rememberDefaultShortcuts(QAction& action);
[[nodiscard]] QList<QKeySequence> defaultShortcuts(const QAction& action);

// The shortcuts of `action` under `overrides` (object name -> portable text, empty = none).
[[nodiscard]] QList<QKeySequence> shortcutsFor(const QAction& action,
                                               const std::map<QString, QString>& overrides);
void applyShortcuts(const std::vector<QAction*>& actions,
                    const std::map<QString, QString>& overrides);

// The menu text without accelerator markers and trailing dots ("&Open..." -> "Open").
[[nodiscard]] QString actionLabel(const QAction& action);

struct ShortcutConflict
{
    QKeySequence key;
    QStringList names;  // object names, sorted
};

// Key sequences bound to more than one action, sorted by key.
[[nodiscard]] std::vector<ShortcutConflict>
findShortcutConflicts(const std::vector<std::pair<QString, QList<QKeySequence>>>& bindings);

// Lists the actions with their shortcuts; one is edited at a time (record, clear, reset).
// Conflicts are shown and block OK.
//
// Thread-safety: UI thread only.
class ShortcutsDialog : public QDialog
{
    Q_OBJECT

public:
    ShortcutsDialog(std::vector<QAction*> actions, std::map<QString, QString> overrides,
                    QWidget* parent = nullptr);

    [[nodiscard]] const std::map<QString, QString>& overrides() const noexcept
    {
        return m_overrides;
    }

    // Editing by object name (also used by the UI; public for tests). An empty sequence
    // removes the shortcut.
    void setShortcut(const QString& name, const QKeySequence& key);
    void resetShortcut(const QString& name);
    void resetAll();

    [[nodiscard]] std::vector<ShortcutConflict> conflicts() const;

    [[nodiscard]] QTreeWidget* list() const noexcept { return m_list; }

    [[nodiscard]] QLabel* conflictLabel() const noexcept { return m_conflicts; }

    void accept() override;

private:
    [[nodiscard]] QList<QKeySequence> current(const QAction& action) const;
    [[nodiscard]] QAction* actionNamed(const QString& name) const;
    [[nodiscard]] QString selectedName() const;
    void refresh();

    std::vector<QAction*> m_actions;
    std::map<QString, QString> m_overrides;
    QTreeWidget* m_list = nullptr;
    QKeySequenceEdit* m_edit = nullptr;
    QLabel* m_conflicts = nullptr;
};

}  // namespace lv::ui
