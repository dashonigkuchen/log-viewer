#include "ui/filter_bar.h"

#include "model/filter_history.h"
#include "model/filtered_document.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lv::ui
{

namespace
{

QString number(std::uint64_t value)
{
    return QLocale().toString(static_cast<qulonglong>(value));
}

QComboBox* makeEditableCombo(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    combo->setEditable(true);  // creates the line edit
    return combo;
}

}  // namespace

FilterBar::FilterBar(model::FilteredDocument& filter, QWidget* parent)
    : QWidget(parent)
    , m_filter(&filter)
    , m_combo(makeEditableCombo(this))
    , m_input(m_combo->lineEdit())
    , m_mode(new QComboBox(this))
    , m_caseSensitive(new QCheckBox(QStringLiteral("Case sensitive"), this))
    , m_wholeWord(new QCheckBox(QStringLiteral("Whole word"), this))
    , m_invert(new QCheckBox(QStringLiteral("Invert"), this))
    , m_clear(new QPushButton(QStringLiteral("Clear"), this))
    , m_status(new QLabel(this))
    , m_error(new QLabel(this))
{
    setObjectName(QStringLiteral("filterBar"));
    m_combo->setObjectName(QStringLiteral("filterHistory"));
    m_combo->setInsertPolicy(QComboBox::NoInsert);  // the history decides what is stored
    m_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_combo->setMinimumContentsLength(20);
    m_combo->setMaxVisibleItems(15);
    auto* completer = new QCompleter(m_combo->model(), m_combo);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    m_combo->setCompleter(completer);
    m_combo->view()->installEventFilter(this);
    m_input->setObjectName(QStringLiteral("filterInput"));
    m_input->setPlaceholderText(QStringLiteral("Filter (Enter to apply)"));
    m_input->setClearButtonEnabled(false);
    m_input->installEventFilter(this);
    m_mode->setObjectName(QStringLiteral("filterMode"));
    m_mode->addItem(QStringLiteral("Literal"), static_cast<int>(core::QueryMode::kLiteral));
    m_mode->addItem(QStringLiteral("Regex"), static_cast<int>(core::QueryMode::kRegex));
    m_mode->addItem(QStringLiteral("Boolean"), static_cast<int>(core::QueryMode::kBoolean));
    m_mode->setToolTip(
        QStringLiteral("Boolean: \"text\" and /regex/ terms with AND, OR, NOT, and parentheses"));
    m_caseSensitive->setObjectName(QStringLiteral("filterCaseSensitive"));
    m_wholeWord->setObjectName(QStringLiteral("filterWholeWord"));
    m_invert->setObjectName(QStringLiteral("filterInvert"));
    m_clear->setObjectName(QStringLiteral("filterClear"));
    m_status->setObjectName(QStringLiteral("filterStatus"));
    m_error->setObjectName(QStringLiteral("filterError"));
    m_error->setStyleSheet(QStringLiteral("color: #c62828;"));
    m_error->setWordWrap(true);
    m_error->hide();

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_mode);
    row->addWidget(m_combo, 1);
    row->addWidget(m_caseSensitive);
    row->addWidget(m_wholeWord);
    row->addWidget(m_invert);
    row->addWidget(m_clear);
    row->addWidget(m_status);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);
    layout->addLayout(row);
    layout->addWidget(m_error);

    connect(m_input, &QLineEdit::returnPressed, this, &FilterBar::apply);
    connect(m_input, &QLineEdit::textEdited, this, [this] {
        hideError();
        m_historyPosition = -1;
    });
    connect(m_combo, &QComboBox::activated, this, [this](int index) {
        showHistoryEntry(index);
        apply();
    });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        updateOptions();
        hideError();
    });
    // Option changes by the user apply the entered text right away; text changes wait for
    // Enter. clicked/activated fire only on user interaction, not when showQuery() or the
    // history set the options.
    connect(m_mode, &QComboBox::activated, this, &FilterBar::applyOptionChange);
    for (QCheckBox* box : {m_caseSensitive, m_wholeWord, m_invert}) {
        connect(box, &QCheckBox::clicked, this, &FilterBar::applyOptionChange);
    }
    connect(m_clear, &QPushButton::clicked, this, &FilterBar::clear);
    connect(m_filter, &model::FilteredDocument::stateChanged, this, &FilterBar::updateStatus);
    connect(m_filter, &model::FilteredDocument::progressChanged, this, &FilterBar::updateStatus);
    connect(m_filter, &model::FilteredDocument::reset, this, &FilterBar::updateStatus);
    updateOptions();
    updateStatus();
}

core::Query FilterBar::enteredQuery() const
{
    core::Query query;
    query.text = m_input->text().toStdString();
    query.mode = static_cast<core::QueryMode>(m_mode->currentData().toInt());
    query.caseSensitive = m_caseSensitive->isChecked();
    query.wholeWord = m_wholeWord->isChecked();
    query.invert = m_invert->isChecked();
    return query;
}

void FilterBar::applyOptionChange()
{
    if (m_input->text().isEmpty()) {
        return;  // nothing to filter yet
    }
    QWidget* focused = QApplication::focusWidget();
    apply();
    if (focused != nullptr && m_error->isVisible()) {
        focused->setFocus();  // keep the focus on the option; the error is shown inline
    }
}

bool FilterBar::apply()
{
    if (m_input->text().isEmpty()) {
        clear();
        return true;
    }
    const core::Query query = enteredQuery();
    if (const auto error = m_filter->apply(query)) {
        showError(QString::fromStdString(error->message), error->position);
        return false;
    }
    hideError();
    m_historyPosition = -1;
    if (m_history != nullptr) {
        m_history->add(query);
    }
    return true;
}

void FilterBar::restore()
{
    showQuery(m_filter->query());
    hideError();
}

void FilterBar::clear()
{
    m_input->clear();
    hideError();
    m_filter->clear();
}

void FilterBar::setHistory(model::FilterHistory* history)
{
    if (m_history != nullptr) {
        disconnect(m_history, nullptr, this, nullptr);
    }
    m_history = history;
    if (m_history != nullptr) {
        connect(m_history, &model::FilterHistory::changed, this, &FilterBar::refreshHistory);
    }
    refreshHistory();
}

void FilterBar::refreshHistory()
{
    const QString text = m_input->text();
    const int cursor = m_input->cursorPosition();
    m_combo->blockSignals(true);
    m_combo->clear();
    if (m_history != nullptr) {
        for (const auto& query : m_history->entries()) {
            QString options = QString::fromLatin1(core::toString(query.mode).data());
            if (query.caseSensitive) {
                options += QStringLiteral(", case sensitive");
            }
            if (query.wholeWord) {
                options += QStringLiteral(", whole word");
            }
            if (query.invert) {
                options += QStringLiteral(", invert");
            }
            m_combo->addItem(QString::fromStdString(query.text));
            m_combo->setItemData(m_combo->count() - 1, options, Qt::ToolTipRole);
        }
    }
    m_combo->setCurrentIndex(-1);
    m_combo->blockSignals(false);
    m_input->setText(text);
    m_input->setCursorPosition(cursor);
}

void FilterBar::showHistoryEntry(int index)
{
    if (m_history == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= m_history->entries().size()) {
        return;
    }
    showQuery(m_history->entries()[static_cast<std::size_t>(index)]);
}

void FilterBar::cycleHistory(int step)
{
    if (m_history == nullptr || m_history->entries().empty()) {
        return;
    }
    const int count = static_cast<int>(m_history->entries().size());
    const int next = std::clamp(m_historyPosition + step, -1, count - 1);
    if (next == m_historyPosition) {
        return;
    }
    m_historyPosition = next;
    if (next < 0) {
        showQuery(m_filter->query());  // back to the applied filter
    } else {
        showHistoryEntry(next);
    }
    hideError();
}

void FilterBar::showQuery(const std::optional<core::Query>& query)
{
    const core::Query shown = query.value_or(core::Query{});
    m_input->setText(QString::fromStdString(shown.text));
    m_mode->setCurrentIndex(m_mode->findData(static_cast<int>(shown.mode)));
    m_caseSensitive->setChecked(shown.caseSensitive);
    m_wholeWord->setChecked(shown.wholeWord);
    m_invert->setChecked(shown.invert);
}

void FilterBar::showError(const QString& message, std::size_t position)
{
    m_error->setText(QStringLiteral("Error at position %1: %2")
                         .arg(static_cast<qulonglong>(position) + 1)
                         .arg(message));
    m_error->show();
    m_input->setFocus();
    m_input->setCursorPosition(static_cast<int>(position));
}

void FilterBar::hideError()
{
    m_error->clear();
    m_error->hide();
}

void FilterBar::updateOptions()
{
    // Whole word applies to literal filters only (F-FILT-2).
    m_wholeWord->setEnabled(static_cast<core::QueryMode>(m_mode->currentData().toInt()) ==
                            core::QueryMode::kLiteral);
}

void FilterBar::updateStatus()
{
    using State = model::FilteredDocument::State;
    const auto& progress = m_filter->progress();
    const QString matches = QStringLiteral("%1 matches").arg(number(m_filter->matchCount()));
    QString text;
    switch (m_filter->state()) {
    case State::kInactive:
        text.clear();
        break;
    case State::kPending:
        text = QStringLiteral("Waiting for indexing");
        break;
    case State::kSearching: {
        const std::uint64_t total = progress.totalLines.get();
        const std::uint64_t percent = total == 0 ? 0 : progress.searchedLines.get() * 100 / total;
        text = QStringLiteral("Searching %1% | %2").arg(percent).arg(matches);
        break;
    }
    case State::kDone:
        text = matches;
        break;
    case State::kError:
        text = QStringLiteral("Search failed: %1").arg(m_filter->errorMessage());
        break;
    }
    if (progress.skippedLines > 0) {
        text += QStringLiteral(" | %1 lines skipped: regex too complex")
                    .arg(number(progress.skippedLines));
    }
    m_status->setText(text);
}

bool FilterBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);  // NOLINT(*-static-cast-downcast)
        if (key->key() == Qt::Key_Escape) {
            restore();
            return true;
        }
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
            if (m_combo->completer() != nullptr && m_combo->completer()->popup()->isVisible()) {
                return false;  // let the completion popup handle the keys
            }
            cycleHistory(key->key() == Qt::Key_Up ? 1 : -1);
            return true;
        }
    }
    if (watched == m_combo->view() && event->type() == QEvent::KeyPress && m_history != nullptr) {
        const auto* key = static_cast<QKeyEvent*>(event);  // NOLINT(*-static-cast-downcast)
        const int row = m_combo->view()->currentIndex().row();
        if (key->key() == Qt::Key_Delete && row >= 0) {
            m_history->remove(static_cast<std::size_t>(row));  // F-HIST-3
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace lv::ui
