#include "ui/shortcuts.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace lv::ui
{
namespace
{

constexpr auto kDefaultsProperty = "lvDefaultShortcuts";

QString shortcutText(const QList<QKeySequence>& keys)
{
    QStringList parts;
    for (const QKeySequence& key : keys) {
        parts << key.toString(QKeySequence::NativeText);
    }
    return parts.join(QStringLiteral(", "));
}

}  // namespace

void rememberDefaultShortcuts(QAction& action)
{
    if (!action.property(kDefaultsProperty).isValid()) {
        action.setProperty(kDefaultsProperty, QVariant::fromValue(action.shortcuts()));
    }
}

QList<QKeySequence> defaultShortcuts(const QAction& action)
{
    const QVariant value = action.property(kDefaultsProperty);
    return value.isValid() ? value.value<QList<QKeySequence>>() : action.shortcuts();
}

QList<QKeySequence> shortcutsFor(const QAction& action, const std::map<QString, QString>& overrides)
{
    const auto it = overrides.find(action.objectName());
    if (it == overrides.end()) {
        return defaultShortcuts(action);
    }
    const QKeySequence key = QKeySequence::fromString(it->second, QKeySequence::PortableText);
    return key.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{key};
}

void applyShortcuts(const std::vector<QAction*>& actions,
                    const std::map<QString, QString>& overrides)
{
    for (QAction* action : actions) {
        rememberDefaultShortcuts(*action);
        action->setShortcuts(shortcutsFor(*action, overrides));
    }
}

QString actionLabel(const QAction& action)
{
    QString text = action.text();
    text.remove(QRegularExpression(QStringLiteral("&(?!&)")));
    while (text.endsWith(u'.') || text.endsWith(QChar(0x2026))) {
        text.chop(1);
    }
    return text;
}

std::vector<ShortcutConflict>
findShortcutConflicts(const std::vector<std::pair<QString, QList<QKeySequence>>>& bindings)
{
    std::map<QString, ShortcutConflict> byKey;  // portable text -> users
    for (const auto& [name, keys] : bindings) {
        for (const QKeySequence& key : keys) {
            if (key.isEmpty()) {
                continue;
            }
            ShortcutConflict& entry = byKey[key.toString(QKeySequence::PortableText)];
            entry.key = key;
            if (!entry.names.contains(name)) {
                entry.names << name;
            }
        }
    }
    std::vector<ShortcutConflict> result;
    for (auto& [text, entry] : byKey) {
        if (entry.names.size() > 1) {
            entry.names.sort();
            result.push_back(std::move(entry));
        }
    }
    return result;
}

ShortcutsDialog::ShortcutsDialog(std::vector<QAction*> actions,
                                 std::map<QString, QString> overrides, QWidget* parent)
    : QDialog(parent)
    , m_actions(std::move(actions))
    , m_overrides(std::move(overrides))
    , m_list(new QTreeWidget(this))
    , m_edit(new QKeySequenceEdit(this))
    , m_conflicts(new QLabel(this))
{
    setObjectName(QStringLiteral("shortcutsDialog"));
    setWindowTitle(QStringLiteral("Keyboard Shortcuts"));
    resize(560, 520);
    std::ranges::sort(m_actions, [](const QAction* left, const QAction* right) {
        return actionLabel(*left).compare(actionLabel(*right), Qt::CaseInsensitive) < 0;
    });

    m_list->setObjectName(QStringLiteral("shortcutList"));
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({QStringLiteral("Command"), QStringLiteral("Shortcut")});
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (const QAction* action : m_actions) {
        auto* item = new QTreeWidgetItem(m_list);
        item->setText(0, actionLabel(*action));
        item->setData(0, Qt::UserRole, action->objectName());
    }

    m_edit->setObjectName(QStringLiteral("shortcutEdit"));
    m_edit->setMaximumSequenceLength(1);
    m_edit->setClearButtonEnabled(true);
    auto* reset = new QPushButton(QStringLiteral("Reset"), this);
    auto* resetAllButton = new QPushButton(QStringLiteral("Reset All"), this);
    auto* editRow = new QHBoxLayout;
    editRow->addWidget(new QLabel(QStringLiteral("Shortcut:"), this));
    editRow->addWidget(m_edit, 1);
    editRow->addWidget(reset);
    editRow->addWidget(resetAllButton);

    m_conflicts->setObjectName(QStringLiteral("shortcutConflicts"));
    m_conflicts->setWordWrap(true);
    QPalette warning = m_conflicts->palette();
    warning.setColor(QPalette::WindowText, QColor(200, 40, 40));
    m_conflicts->setPalette(warning);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_list, 1);
    layout->addLayout(editRow);
    layout->addWidget(m_conflicts);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &ShortcutsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this] {
        const QAction* action = actionNamed(selectedName());
        const QSignalBlocker blocker(m_edit);
        const QList<QKeySequence> keys =
            action != nullptr ? current(*action) : QList<QKeySequence>{};
        m_edit->setKeySequence(keys.isEmpty() ? QKeySequence() : keys.first());
        m_edit->setEnabled(action != nullptr);
    });
    connect(m_edit, &QKeySequenceEdit::keySequenceChanged, this, [this](const QKeySequence& key) {
        if (const QString name = selectedName(); !name.isEmpty()) {
            setShortcut(name, key);
        }
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (const QString name = selectedName(); !name.isEmpty()) {
            resetShortcut(name);
            emit m_list->currentItemChanged(m_list->currentItem(), nullptr);
        }
    });
    connect(resetAllButton, &QPushButton::clicked, this, [this] {
        resetAll();
        emit m_list->currentItemChanged(m_list->currentItem(), nullptr);
    });
    if (m_list->topLevelItemCount() > 0) {
        m_list->setCurrentItem(m_list->topLevelItem(0));
    }
    refresh();
}

QList<QKeySequence> ShortcutsDialog::current(const QAction& action) const
{
    return shortcutsFor(action, m_overrides);
}

QAction* ShortcutsDialog::actionNamed(const QString& name) const
{
    const auto it = std::ranges::find_if(
        m_actions, [&name](const QAction* action) { return action->objectName() == name; });
    return it != m_actions.end() ? *it : nullptr;
}

QString ShortcutsDialog::selectedName() const
{
    const QTreeWidgetItem* item = m_list->currentItem();
    return item != nullptr ? item->data(0, Qt::UserRole).toString() : QString();
}

void ShortcutsDialog::setShortcut(const QString& name, const QKeySequence& key)
{
    const QAction* action = actionNamed(name);
    if (action == nullptr) {
        return;
    }
    const QList<QKeySequence> defaults = defaultShortcuts(*action);
    if ((key.isEmpty() && defaults.isEmpty()) ||
        (!defaults.isEmpty() && defaults.first() == key && defaults.size() == 1)) {
        m_overrides.erase(name);  // back to the default
    } else {
        m_overrides[name] = key.toString(QKeySequence::PortableText);
    }
    refresh();
}

void ShortcutsDialog::resetShortcut(const QString& name)
{
    m_overrides.erase(name);
    refresh();
}

void ShortcutsDialog::resetAll()
{
    m_overrides.clear();
    refresh();
}

std::vector<ShortcutConflict> ShortcutsDialog::conflicts() const
{
    std::vector<std::pair<QString, QList<QKeySequence>>> bindings;
    bindings.reserve(m_actions.size());
    for (const QAction* action : m_actions) {
        bindings.emplace_back(action->objectName(), current(*action));
    }
    return findShortcutConflicts(bindings);
}

void ShortcutsDialog::refresh()
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_list->topLevelItem(i);
        const QString name = item->data(0, Qt::UserRole).toString();
        if (const QAction* action = actionNamed(name)) {
            item->setText(1, shortcutText(current(*action)));
            QFont font = item->font(0);
            font.setBold(m_overrides.contains(name));  // customized
            item->setFont(0, font);
            item->setFont(1, font);
        }
    }
    QStringList lines;
    for (const ShortcutConflict& conflict : conflicts()) {
        QStringList labels;
        for (const QString& name : conflict.names) {
            labels << actionLabel(*actionNamed(name));
        }
        lines << QStringLiteral("%1 is used by %2")
                     .arg(conflict.key.toString(QKeySequence::NativeText),
                          labels.join(QStringLiteral(" and ")));
    }
    m_conflicts->setText(lines.join(u'\n'));
    m_conflicts->setVisible(!lines.isEmpty());
}

void ShortcutsDialog::accept()
{
    if (!conflicts().empty()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("Some shortcuts are used twice. Please change them "
                                            "first.\n\n%1")
                                 .arg(m_conflicts->text()));
        return;
    }
    QDialog::accept();
}

}  // namespace lv::ui
