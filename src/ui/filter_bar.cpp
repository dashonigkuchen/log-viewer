#include "ui/filter_bar.h"

#include "model/filtered_document.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace lv::ui
{

namespace
{

QString number(std::uint64_t value)
{
    return QLocale().toString(static_cast<qulonglong>(value));
}

}  // namespace

FilterBar::FilterBar(model::FilteredDocument& filter, QWidget* parent)
    : QWidget(parent)
    , m_filter(&filter)
    , m_input(new QLineEdit(this))
    , m_mode(new QComboBox(this))
    , m_caseSensitive(new QCheckBox(QStringLiteral("Case sensitive"), this))
    , m_wholeWord(new QCheckBox(QStringLiteral("Whole word"), this))
    , m_invert(new QCheckBox(QStringLiteral("Invert"), this))
    , m_clear(new QPushButton(QStringLiteral("Clear"), this))
    , m_status(new QLabel(this))
    , m_error(new QLabel(this))
{
    setObjectName(QStringLiteral("filterBar"));
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
    row->addWidget(m_input, 1);
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
    connect(m_input, &QLineEdit::textEdited, this, &FilterBar::hideError);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        updateOptions();
        hideError();
    });
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

bool FilterBar::apply()
{
    if (m_input->text().isEmpty()) {
        clear();
        return true;
    }
    if (const auto error = m_filter->apply(enteredQuery())) {
        showError(QString::fromStdString(error->message), error->position);
        return false;
    }
    hideError();
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
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace lv::ui
