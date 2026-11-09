#include "ui/highlight_dialog.h"

#include "model/line_highlighter.h"
#include "model/static_line_source.h"
#include "ui/log_view.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace lv::ui
{
namespace
{

constexpr int kRuleIndexRole = Qt::UserRole;

QStringList samplePreview()
{
    return {QStringLiteral("2026-11-09 10:15:02.114 INFO  [main] Application started"),
            QStringLiteral("2026-11-09 10:15:02.512 DEBUG [pool-1] Connecting to db:5432"),
            QStringLiteral("2026-11-09 10:15:03.020 WARN  [pool-1] Slow query took 1250 ms"),
            QStringLiteral("2026-11-09 10:15:04.877 ERROR [http-7] Request failed: timeout"),
            QStringLiteral("2026-11-09 10:15:05.001 INFO  [http-7] user=42 action=login"),
            QStringLiteral("2026-11-09 10:15:06.338 FATAL [main] Out of memory")};
}

// `name` (or `fallback` if empty), with " (2)", " (3)", ... appended until no set other than
// `except` uses it.
QString uniqueName(const std::vector<model::HighlightSet>& sets, const QString& name,
                   const model::HighlightSet* except, const QString& fallback)
{
    const QString base = name.trimmed().isEmpty() ? fallback : name.trimmed();
    QString candidate = base;
    for (int n = 2;; ++n) {
        const bool taken = std::ranges::any_of(sets, [&](const model::HighlightSet& set) {
            return &set != except && set.name == candidate;
        });
        if (!taken) {
            return candidate;
        }
        candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
    }
}

QPushButton* button(const QString& text, const QString& name, QWidget* parent)
{
    auto* result = new QPushButton(text, parent);
    result->setObjectName(name);
    result->setAutoDefault(false);
    return result;
}

}  // namespace

HighlightDialog::HighlightDialog(const model::HighlightSets& sets, QStringList previewLines,
                                 QWidget* parent)
    : QDialog(parent)
    , m_sets(sets.sets())
    , m_default(sets.defaultIndex())
    , m_previewSets(std::make_unique<model::HighlightSets>())
    , m_previewHighlighter(std::make_unique<model::LineHighlighter>())
    , m_previewSource(std::make_unique<model::StaticLineSource>(
          previewLines.isEmpty() ? samplePreview() : std::move(previewLines)))
{
    setObjectName(QStringLiteral("highlightDialog"));
    setWindowTitle(QStringLiteral("Highlight Rules"));
    resize(960, 640);
    m_openFile = [this] {
        return QFileDialog::getOpenFileName(this, QStringLiteral("Import Highlight Sets"), {},
                                            QStringLiteral("Highlight sets (*.json)"));
    };
    m_saveFile = [this] {
        return QFileDialog::getSaveFileName(this, QStringLiteral("Export Highlight Sets"),
                                            QStringLiteral("highlight-sets.json"),
                                            QStringLiteral("Highlight sets (*.json)"));
    };
    buildLayout();
    m_previewHighlighter->setSets(m_previewSets.get());
    m_preview->setLineSource(m_previewSource.get());
    m_preview->setLineHighlighter(m_previewHighlighter.get());
    rebuildSetList();
    selectSet(m_default);
}

HighlightDialog::~HighlightDialog()
{
    m_preview->setLineHighlighter(nullptr);
    m_preview->setLineSource(nullptr);
}

void HighlightDialog::buildLayout()
{
    // Sets.
    auto* setBox = new QGroupBox(QStringLiteral("Highlight sets"), this);
    m_setList = new QListWidget(setBox);
    m_setList->setObjectName(QStringLiteral("setList"));
    auto* setButtons = new QHBoxLayout;
    const auto addSetButton = [&](const QString& text, const QString& name, auto slot) {
        auto* b = button(text, name, setBox);
        connect(b, &QPushButton::clicked, this, slot);
        setButtons->addWidget(b);
    };
    addSetButton(QStringLiteral("New"), QStringLiteral("newSet"), &HighlightDialog::addSet);
    addSetButton(QStringLiteral("Duplicate"), QStringLiteral("duplicateSet"),
                 &HighlightDialog::duplicateSet);
    addSetButton(QStringLiteral("Delete"), QStringLiteral("deleteSet"),
                 &HighlightDialog::deleteSet);
    addSetButton(QStringLiteral("Make Default"), QStringLiteral("makeDefault"),
                 &HighlightDialog::makeDefault);
    auto* fileButtons = new QHBoxLayout;
    auto* importButton = button(QStringLiteral("Import..."), QStringLiteral("importSets"), setBox);
    auto* exportButton = button(QStringLiteral("Export..."), QStringLiteral("exportSets"), setBox);
    connect(importButton, &QPushButton::clicked, this, &HighlightDialog::importSets);
    connect(exportButton, &QPushButton::clicked, this, &HighlightDialog::exportSets);
    fileButtons->addWidget(importButton);
    fileButtons->addWidget(exportButton);
    fileButtons->addStretch();
    auto* setLayout = new QVBoxLayout(setBox);
    setLayout->addWidget(m_setList);
    setLayout->addLayout(setButtons);
    setLayout->addLayout(fileButtons);

    // Rules.
    auto* ruleBox = new QGroupBox(QStringLiteral("Rules (first matching line rule wins)"), this);
    m_ruleList = new QListWidget(ruleBox);
    m_ruleList->setObjectName(QStringLiteral("ruleList"));
    m_ruleList->setDragDropMode(QAbstractItemView::InternalMove);
    m_ruleList->setDefaultDropAction(Qt::MoveAction);
    auto* ruleButtons = new QHBoxLayout;
    const auto addRuleButton = [&](const QString& text, const QString& name, auto slot) {
        auto* b = button(text, name, ruleBox);
        connect(b, &QPushButton::clicked, this, slot);
        ruleButtons->addWidget(b);
    };
    addRuleButton(QStringLiteral("Add"), QStringLiteral("addRule"), &HighlightDialog::addRule);
    addRuleButton(QStringLiteral("Duplicate"), QStringLiteral("duplicateRule"),
                  &HighlightDialog::duplicateRule);
    addRuleButton(QStringLiteral("Delete"), QStringLiteral("deleteRule"),
                  &HighlightDialog::deleteRule);
    addRuleButton(QStringLiteral("Up"), QStringLiteral("moveRuleUp"), [this] { moveRule(-1); });
    addRuleButton(QStringLiteral("Down"), QStringLiteral("moveRuleDown"), [this] { moveRule(1); });

    // Rule editor.
    m_ruleEditor = new QWidget(ruleBox);
    m_pattern = new QLineEdit(m_ruleEditor);
    m_pattern->setObjectName(QStringLiteral("pattern"));
    m_regex = new QCheckBox(QStringLiteral("Regular expression"), m_ruleEditor);
    m_caseSensitive = new QCheckBox(QStringLiteral("Case sensitive"), m_ruleEditor);
    m_scope = new QComboBox(m_ruleEditor);
    m_scope->addItem(QStringLiteral("Whole line"));
    m_scope->addItem(QStringLiteral("Match only"));
    m_foreground = new QLineEdit(m_ruleEditor);
    m_foreground->setObjectName(QStringLiteral("foreground"));
    m_foreground->setPlaceholderText(QStringLiteral("#RRGGBB (empty = unchanged)"));
    m_background = new QLineEdit(m_ruleEditor);
    m_background->setObjectName(QStringLiteral("background"));
    m_background->setPlaceholderText(QStringLiteral("#RRGGBB (empty = unchanged)"));
    m_error = new QLabel(m_ruleEditor);
    m_error->setObjectName(QStringLiteral("ruleError"));
    m_error->setStyleSheet(QStringLiteral("color: #c62828"));
    m_error->setWordWrap(true);
    const auto colorRow = [this](QLineEdit* edit) {
        auto* row = new QHBoxLayout;
        row->addWidget(edit);
        auto* pick = button(QStringLiteral("..."), QString(), m_ruleEditor);
        pick->setMaximumWidth(32);
        connect(pick, &QPushButton::clicked, this, [this, edit] { pickColor(edit); });
        row->addWidget(pick);
        return row;
    };
    auto* form = new QFormLayout(m_ruleEditor);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QStringLiteral("Pattern:"), m_pattern);
    auto* options = new QHBoxLayout;
    options->addWidget(m_regex);
    options->addWidget(m_caseSensitive);
    options->addStretch();
    form->addRow(QString(), options);
    form->addRow(QStringLiteral("Scope:"), m_scope);
    form->addRow(QStringLiteral("Text color:"), colorRow(m_foreground));
    form->addRow(QStringLiteral("Background:"), colorRow(m_background));
    form->addRow(QString(), m_error);

    auto* ruleLayout = new QVBoxLayout(ruleBox);
    ruleLayout->addWidget(m_ruleList, 1);
    ruleLayout->addLayout(ruleButtons);
    ruleLayout->addWidget(m_ruleEditor);

    // Preview.
    auto* previewBox = new QGroupBox(QStringLiteral("Preview"), this);
    m_preview = new LogView(previewBox);
    m_preview->setObjectName(QStringLiteral("highlightPreview"));
    m_preview->setLineNumbersVisible(false);
    auto* previewLayout = new QVBoxLayout(previewBox);
    previewLayout->addWidget(m_preview);

    auto* top = new QSplitter(Qt::Horizontal, this);
    top->addWidget(setBox);
    top->addWidget(ruleBox);
    top->setStretchFactor(1, 2);
    auto* main = new QSplitter(Qt::Vertical, this);
    main->addWidget(top);
    main->addWidget(previewBox);
    main->setStretchFactor(0, 3);
    main->setStretchFactor(1, 1);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(main, 1);
    layout->addWidget(buttons);

    connect(m_setList, &QListWidget::currentRowChanged, this, [this] {
        if (!m_updating) {
            rebuildRuleList();
        }
    });
    connect(m_setList, &QListWidget::itemChanged, this, &HighlightDialog::onSetItemChanged);
    connect(m_ruleList, &QListWidget::currentRowChanged, this, [this] {
        if (!m_updating) {
            showRule();
        }
    });
    connect(m_ruleList, &QListWidget::itemChanged, this, &HighlightDialog::onRuleItemChanged);
    connect(m_ruleList->model(), &QAbstractItemModel::rowsMoved, this,
            &HighlightDialog::onRulesMoved);
    for (QLineEdit* edit : {m_pattern, m_foreground, m_background}) {
        connect(edit, &QLineEdit::textEdited, this, &HighlightDialog::storeRule);
    }
    for (QCheckBox* box : {m_regex, m_caseSensitive}) {
        connect(box, &QCheckBox::toggled, this, &HighlightDialog::storeRule);
    }
    connect(m_scope, &QComboBox::currentIndexChanged, this, &HighlightDialog::storeRule);
}

void HighlightDialog::setFileChoosers(std::function<QString()> openFile,
                                      std::function<QString()> saveFile)
{
    m_openFile = std::move(openFile);
    m_saveFile = std::move(saveFile);
}

std::optional<std::optional<model::Argb>> HighlightDialog::parseColor(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::optional<model::Argb>{};
    }
    bool ok = false;
    const uint value = trimmed.mid(1).toUInt(&ok, 16);
    if (!trimmed.startsWith(u'#') || !ok || (trimmed.size() != 7 && trimmed.size() != 9)) {
        return std::nullopt;
    }
    return std::optional<model::Argb>(trimmed.size() == 7 ? (0xFF000000U | value) : value);
}

QString HighlightDialog::formatColor(std::optional<model::Argb> color)
{
    if (!color) {
        return {};
    }
    if ((*color >> 24U) == 0xFFU) {
        return QStringLiteral("#%1").arg(*color & 0xFFFFFFU, 6, 16, QChar(u'0')).toUpper();
    }
    return QStringLiteral("#%1").arg(*color, 8, 16, QChar(u'0')).toUpper();
}

QString HighlightDialog::ruleLabel(const model::HighlightRule& rule)
{
    const QString kind = rule.regex ? QStringLiteral("regex") : QStringLiteral("text");
    const QString scope = rule.scope == model::HighlightScope::kWholeLine ? QStringLiteral("line")
                                                                          : QStringLiteral("match");
    return QStringLiteral("%1   (%2, %3)").arg(rule.pattern, kind, scope);
}

std::optional<std::size_t> HighlightDialog::currentSet() const
{
    const int row = m_setList->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= m_sets.size()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(row);
}

int HighlightDialog::currentRule() const
{
    return m_ruleList->currentRow();
}

model::HighlightSet* HighlightDialog::editedSet()
{
    const auto index = currentSet();
    return index ? &m_sets[*index] : nullptr;
}

void HighlightDialog::selectSet(std::size_t index)
{
    if (index < m_sets.size()) {
        m_setList->setCurrentRow(static_cast<int>(index));
    }
}

void HighlightDialog::selectRule(int rule)
{
    m_ruleList->setCurrentRow(rule);
}

void HighlightDialog::rebuildSetList()
{
    m_updating = true;
    const int row = m_setList->currentRow();
    m_setList->clear();
    for (std::size_t i = 0; i < m_sets.size(); ++i) {
        auto* item = new QListWidgetItem(m_sets[i].name, m_setList);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        QFont font = item->font();
        font.setBold(i == m_default);
        item->setFont(font);
        item->setToolTip(i == m_default ? QStringLiteral("Default set") : QString());
    }
    m_setList->setCurrentRow(std::min(row, static_cast<int>(m_sets.size()) - 1));
    m_updating = false;
    rebuildRuleList();
}

void HighlightDialog::rebuildRuleList()
{
    m_updating = true;
    const int row = m_ruleList->currentRow();
    m_ruleList->clear();
    if (const model::HighlightSet* set = editedSet()) {
        for (std::size_t i = 0; i < set->rules.size(); ++i) {
            const auto& rule = set->rules[i];
            auto* item = new QListWidgetItem(ruleLabel(rule), m_ruleList);
            item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled) &
                           ~Qt::ItemIsDropEnabled);
            item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
            item->setData(kRuleIndexRole, static_cast<qulonglong>(i));
            if (rule.background) {
                item->setBackground(QColor::fromRgba(*rule.background));
            }
            if (rule.foreground) {
                item->setForeground(QColor::fromRgba(*rule.foreground));
            }
        }
    }
    m_ruleList->setCurrentRow(std::min(row, m_ruleList->count() - 1));
    if (m_ruleList->currentRow() < 0 && m_ruleList->count() > 0) {
        m_ruleList->setCurrentRow(0);
    }
    m_updating = false;
    showRule();
    updatePreview();
}

void HighlightDialog::showRule()
{
    const model::HighlightSet* set = editedSet();
    const int index = currentRule();
    const bool valid =
        set != nullptr && index >= 0 && static_cast<std::size_t>(index) < set->rules.size();
    m_ruleEditor->setEnabled(valid);
    m_updating = true;
    const model::HighlightRule rule =
        valid ? set->rules[static_cast<std::size_t>(index)] : model::HighlightRule{};
    m_pattern->setText(rule.pattern);
    m_regex->setChecked(rule.regex);
    m_caseSensitive->setChecked(rule.caseSensitive);
    m_scope->setCurrentIndex(rule.scope == model::HighlightScope::kWholeLine ? 0 : 1);
    m_foreground->setText(formatColor(rule.foreground));
    m_background->setText(formatColor(rule.background));
    m_updating = false;
    m_error->clear();
}

void HighlightDialog::storeRule()
{
    model::HighlightSet* set = editedSet();
    const int index = currentRule();
    if (m_updating || set == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= set->rules.size()) {
        return;
    }
    model::HighlightRule& rule = set->rules[static_cast<std::size_t>(index)];
    rule.pattern = m_pattern->text();
    rule.regex = m_regex->isChecked();
    rule.caseSensitive = m_caseSensitive->isChecked();
    rule.scope = m_scope->currentIndex() == 0 ? model::HighlightScope::kWholeLine
                                              : model::HighlightScope::kMatchOnly;
    QStringList problems;
    if (const auto color = parseColor(m_foreground->text())) {
        rule.foreground = *color;
    } else {
        problems << QStringLiteral("Invalid text color (use #RRGGBB).");
    }
    if (const auto color = parseColor(m_background->text())) {
        rule.background = *color;
    } else {
        problems << QStringLiteral("Invalid background color (use #RRGGBB).");
    }
    const model::CompiledHighlightSet check(model::HighlightSet{.name = {}, .rules = {rule}});
    if (!check.errors().empty()) {
        problems << QStringLiteral("Invalid pattern: %1").arg(check.errors().front().message);
    }
    if (rule.pattern.isEmpty()) {
        problems << QStringLiteral("The pattern is empty; the rule has no effect.");
    }
    m_error->setText(problems.join(u'\n'));
    if (QListWidgetItem* item = m_ruleList->item(index)) {
        m_updating = true;
        item->setText(ruleLabel(rule));
        item->setBackground(rule.background ? QBrush(QColor::fromRgba(*rule.background))
                                            : QBrush());
        item->setForeground(rule.foreground ? QBrush(QColor::fromRgba(*rule.foreground))
                                            : QBrush());
        m_updating = false;
    }
    updatePreview();
}

void HighlightDialog::onRuleItemChanged(QListWidgetItem* item)
{
    model::HighlightSet* set = editedSet();
    if (m_updating || set == nullptr) {
        return;
    }
    const int row = m_ruleList->row(item);
    if (row < 0 || static_cast<std::size_t>(row) >= set->rules.size()) {
        return;
    }
    set->rules[static_cast<std::size_t>(row)].enabled = item->checkState() == Qt::Checked;
    updatePreview();
}

void HighlightDialog::onRulesMoved()
{
    // Drag and drop reordered the items: apply the new order to the rules.
    model::HighlightSet* set = editedSet();
    if (m_updating || set == nullptr) {
        return;
    }
    std::vector<model::HighlightRule> reordered;
    reordered.reserve(set->rules.size());
    for (int row = 0; row < m_ruleList->count(); ++row) {
        const auto old = m_ruleList->item(row)->data(kRuleIndexRole).toULongLong();
        if (old < set->rules.size()) {
            reordered.push_back(set->rules[static_cast<std::size_t>(old)]);
        }
    }
    if (reordered.size() != set->rules.size()) {
        rebuildRuleList();  // unexpected drop: show the unchanged order
        return;
    }
    const int current = m_ruleList->currentRow();
    set->rules = std::move(reordered);
    rebuildRuleList();
    m_ruleList->setCurrentRow(current);
}

void HighlightDialog::onSetItemChanged(QListWidgetItem* item)
{
    if (m_updating) {
        return;
    }
    const int row = m_setList->row(item);
    if (row < 0 || static_cast<std::size_t>(row) >= m_sets.size()) {
        return;
    }
    model::HighlightSet& set = m_sets[static_cast<std::size_t>(row)];
    // An empty name keeps the old one; names must be unique.
    const QString name = uniqueName(m_sets, item->text(), &set, set.name);
    set.name = name;
    m_updating = true;
    item->setText(name);
    m_updating = false;
}

void HighlightDialog::updatePreview()
{
    const model::HighlightSet* set = editedSet();
    m_previewSets->assign({set != nullptr ? *set : model::HighlightSet{}}, 0);
}

void HighlightDialog::pickColor(QLineEdit* edit)
{
    const auto current = parseColor(edit->text());
    const QColor initial = current && *current ? QColor::fromRgba(**current) : QColor(Qt::white);
    const QColor color = QColorDialog::getColor(initial, this, QStringLiteral("Choose Color"));
    if (color.isValid()) {
        edit->setText(formatColor(color.rgba()));
        storeRule();
    }
}

void HighlightDialog::addSet()
{
    const QString name =
        uniqueName(m_sets, QStringLiteral("New Set"), nullptr, QStringLiteral("New Set"));
    m_sets.push_back({.name = name, .rules = {}});
    rebuildSetList();
    selectSet(m_sets.size() - 1);
}

void HighlightDialog::duplicateSet()
{
    const auto index = currentSet();
    if (!index) {
        return;
    }
    model::HighlightSet copy = m_sets[*index];
    copy.name = uniqueName(m_sets, copy.name + QStringLiteral(" copy"), nullptr, copy.name);
    m_sets.push_back(std::move(copy));
    rebuildSetList();
    selectSet(m_sets.size() - 1);
}

void HighlightDialog::deleteSet()
{
    const auto index = currentSet();
    if (!index || m_sets.size() <= 1) {
        return;  // the last set stays
    }
    m_sets.erase(m_sets.begin() + static_cast<std::ptrdiff_t>(*index));
    if (*index < m_default || (*index == m_default && m_default > 0)) {
        --m_default;
    }
    rebuildSetList();
    selectSet(std::min(*index, m_sets.size() - 1));
}

void HighlightDialog::makeDefault()
{
    if (const auto index = currentSet()) {
        m_default = *index;
        rebuildSetList();
    }
}

void HighlightDialog::addRule()
{
    model::HighlightSet* set = editedSet();
    if (set == nullptr) {
        return;
    }
    set->rules.push_back({.pattern = QStringLiteral("text"),
                          .regex = false,
                          .caseSensitive = false,
                          .scope = model::HighlightScope::kWholeLine,
                          .foreground = std::nullopt,
                          .background = 0xFFFFF59DU,
                          .enabled = true});
    rebuildRuleList();
    selectRule(static_cast<int>(set->rules.size()) - 1);
    m_pattern->setFocus();
    m_pattern->selectAll();
}

void HighlightDialog::duplicateRule()
{
    model::HighlightSet* set = editedSet();
    const int index = currentRule();
    if (set == nullptr || index < 0 || static_cast<std::size_t>(index) >= set->rules.size()) {
        return;
    }
    const auto at = set->rules.begin() + index + 1;
    set->rules.insert(at, set->rules[static_cast<std::size_t>(index)]);
    rebuildRuleList();
    selectRule(index + 1);
}

void HighlightDialog::deleteRule()
{
    model::HighlightSet* set = editedSet();
    const int index = currentRule();
    if (set == nullptr || index < 0 || static_cast<std::size_t>(index) >= set->rules.size()) {
        return;
    }
    set->rules.erase(set->rules.begin() + index);
    rebuildRuleList();
    selectRule(std::min(index, static_cast<int>(set->rules.size()) - 1));
}

void HighlightDialog::moveRule(int delta)
{
    model::HighlightSet* set = editedSet();
    const int index = currentRule();
    const int target = index + delta;
    if (set == nullptr || index < 0 || target < 0 ||
        static_cast<std::size_t>(target) >= set->rules.size()) {
        return;
    }
    std::swap(set->rules[static_cast<std::size_t>(index)],
              set->rules[static_cast<std::size_t>(target)]);
    rebuildRuleList();
    selectRule(target);
}

void HighlightDialog::importSets()
{
    const QString path = m_openFile ? m_openFile() : QString();
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Import Highlight Sets"),
                             QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
        return;
    }
    const auto imported = model::HighlightSets::fromJson(file.readAll());
    if (!imported) {
        QMessageBox::warning(this, QStringLiteral("Import Highlight Sets"), imported.error());
        return;
    }
    for (model::HighlightSet set : *imported) {
        set.name = uniqueName(m_sets, set.name, nullptr, QStringLiteral("Imported"));
        m_sets.push_back(std::move(set));
    }
    rebuildSetList();
    selectSet(m_sets.size() - 1);
}

void HighlightDialog::exportSets()
{
    const QString path = m_saveFile ? m_saveFile() : QString();
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(model::HighlightSets::toJson(m_sets)) < 0) {
        QMessageBox::warning(this, QStringLiteral("Export Highlight Sets"),
                             QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    }
}

}  // namespace lv::ui
