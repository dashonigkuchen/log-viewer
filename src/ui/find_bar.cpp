#include "ui/find_bar.h"

#include "model/document.h"
#include "model/filtered_document.h"
#include "model/match_highlighter.h"
#include "ui/log_view.h"
#include "ui/text_selection.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lv::ui
{

FindBar::FindBar(model::Document& document, model::FilteredDocument& filter, LogView& mainView,
                 LogView& filteredView, QWidget* parent)
    : QWidget(parent)
    , m_document(&document)
    , m_filter(&filter)
    , m_mainView(&mainView)
    , m_filteredView(&filteredView)
    , m_target(&mainView)
    , m_highlighter(std::make_unique<model::MatchHighlighter>(document))
    , m_finder(new model::LineFinder(this))
    , m_input(new QLineEdit(this))
    , m_caseSensitive(new QCheckBox(QStringLiteral("Case sensitive"), this))
    , m_wholeWord(new QCheckBox(QStringLiteral("Whole word"), this))
    , m_regex(new QCheckBox(QStringLiteral("Regex"), this))
    , m_previous(new QPushButton(QStringLiteral("Previous"), this))
    , m_next(new QPushButton(QStringLiteral("Next"), this))
    , m_close(new QPushButton(QStringLiteral("Close"), this))
    , m_status(new QLabel(this))
    , m_error(new QLabel(this))
{
    setObjectName(QStringLiteral("findBar"));
    m_input->setObjectName(QStringLiteral("findInput"));
    m_input->setPlaceholderText(QStringLiteral("Find (Enter: next, Shift+Enter: previous)"));
    m_input->setClearButtonEnabled(true);
    m_input->installEventFilter(this);
    m_caseSensitive->setObjectName(QStringLiteral("findCaseSensitive"));
    m_wholeWord->setObjectName(QStringLiteral("findWholeWord"));
    m_regex->setObjectName(QStringLiteral("findRegex"));
    m_previous->setObjectName(QStringLiteral("findPrevious"));
    m_next->setObjectName(QStringLiteral("findNext"));
    m_close->setObjectName(QStringLiteral("findClose"));
    m_status->setObjectName(QStringLiteral("findStatus"));
    m_error->setObjectName(QStringLiteral("findError"));
    m_error->setStyleSheet(QStringLiteral("color: #c62828;"));
    m_error->setWordWrap(true);
    m_error->hide();

    auto* row = new QHBoxLayout;
    row->setContentsMargins(4, 2, 4, 2);
    row->addWidget(m_input, 1);
    row->addWidget(m_caseSensitive);
    row->addWidget(m_wholeWord);
    row->addWidget(m_regex);
    row->addWidget(m_previous);
    row->addWidget(m_next);
    row->addWidget(m_status);
    row->addWidget(m_close);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(row);
    layout->addWidget(m_error);

    m_mainView->setFindHighlighter(m_highlighter.get());
    m_filteredView->setFindHighlighter(m_highlighter.get());

    connect(m_input, &QLineEdit::textChanged, this, &FindBar::updateHighlight);
    for (QCheckBox* box : {m_caseSensitive, m_wholeWord, m_regex}) {
        connect(box, &QCheckBox::toggled, this, &FindBar::updateHighlight);
    }
    connect(m_regex, &QCheckBox::toggled, m_wholeWord, &QCheckBox::setDisabled);
    connect(m_next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(m_previous, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(m_close, &QPushButton::clicked, this, &FindBar::close);
    connect(m_finder, &model::LineFinder::finished, this, &FindBar::showResult);
    // A new file generation invalidates line numbers of a running search.
    connect(m_document, &model::Document::reset, m_finder, &model::LineFinder::cancel);
    connect(m_filter, &model::FilteredDocument::reset, m_finder, &model::LineFinder::cancel);
    hide();
}

FindBar::~FindBar()
{
    m_finder->cancel();
    m_mainView->setFindHighlighter(nullptr);
    m_filteredView->setFindHighlighter(nullptr);
}

void FindBar::open(LogView* target)
{
    m_target = target != nullptr ? target : m_mainView;
    show();
    updateHighlight();
    m_input->setFocus(Qt::ShortcutFocusReason);
    m_input->selectAll();
}

void FindBar::close()
{
    m_finder->cancel();
    m_searching.reset();
    m_highlighter->setQuery(nullptr);
    m_status->clear();
    m_error->hide();
    hide();
    m_target->setFocus(Qt::OtherFocusReason);
}

void FindBar::findNext()
{
    find(model::FindDirection::kForward);
}

void FindBar::findPrevious()
{
    find(model::FindDirection::kBackward);
}

core::Query FindBar::enteredQuery() const
{
    core::Query query;
    query.text = m_input->text().toStdString();
    query.mode = m_regex->isChecked() ? core::QueryMode::kRegex : core::QueryMode::kLiteral;
    query.caseSensitive = m_caseSensitive->isChecked();
    query.wholeWord = m_wholeWord->isChecked();
    return query.normalized();
}

std::shared_ptr<const core::CompiledQuery> FindBar::compile()
{
    const core::Query query = enteredQuery();
    if (query.text.empty()) {
        m_error->hide();
        return nullptr;
    }
    auto compiled = core::CompiledQuery::compile(query);
    if (!compiled) {
        m_error->setText(QStringLiteral("Error at position %1: %2")
                             .arg(static_cast<qulonglong>(compiled.error().position) + 1)
                             .arg(QString::fromStdString(compiled.error().message)));
        m_error->show();
        return nullptr;
    }
    m_error->hide();
    return *std::move(compiled);
}

void FindBar::updateHighlight()
{
    if (!isVisible()) {
        return;
    }
    m_highlighter->setQuery(compile());
    m_status->clear();
}

void FindBar::find(model::FindDirection direction)
{
    if (isHidden()) {
        open(m_target);
    }
    auto compiled = compile();
    if (!compiled) {
        if (m_error->isHidden()) {
            m_input->setFocus();  // nothing entered yet
        }
        return;  // F-FIND-4: no search for an invalid query
    }
    const bool filtered = m_target == m_filteredView;
    std::shared_ptr<const core::SortedU64Sequence> restrict;
    if (filtered) {
        restrict = m_filter->matches();
        if (!restrict) {
            m_status->setText(QStringLiteral("No filter"));
            emit searchFinished();
            return;
        }
    }
    std::optional<std::uint64_t> from;
    if (const auto row = m_target->currentRow(); row && m_target->lineSource() != nullptr) {
        from = m_target->lineSource()->originalLineNumber(*row).get();
    }
    m_searching = compiled;
    m_searchView = m_target;
    m_searchDirection = direction;
    m_status->setText(QStringLiteral("Searching..."));
    m_finder->start(m_document->content(), std::move(compiled), std::move(restrict), from,
                    direction);
}

void FindBar::showResult(const model::FindResult& result)
{
    if (result.failed) {
        m_status->setText(QStringLiteral("Search failed"));
    } else if (!result.line) {
        m_status->setText(QStringLiteral("No matches"));
    } else {
        LogView* view = m_searchView;
        core::LineNumber row(*result.line);
        if (view == m_filteredView) {
            row = core::LineNumber(m_filter->lowerBound(row));
        }
        view->goToRow(row);
        // Select the first match in the line so that it stands out from the other highlights.
        if (m_searching && view->lineSource() != nullptr) {
            const auto lines = view->lineSource()->lines(row, core::LineCount(1));
            if (!lines.empty()) {
                core::MatchScratch scratch;
                const auto ranges =
                    model::highlightRanges(*m_searching, lines.front().text, scratch);
                if (!ranges.empty()) {
                    view->setSelection(
                        TextSelection::characters({.row = row.get(), .column = ranges.front().from},
                                                  {.row = row.get(), .column = ranges.front().to}));
                }
            }
        }
        if (result.wrapped) {
            m_status->setText(m_searchDirection == model::FindDirection::kForward
                                  ? QStringLiteral("Reached the end, continued from the top")
                                  : QStringLiteral("Reached the top, continued from the bottom"));
        } else {
            m_status->clear();
        }
    }
    emit searchFinished();
}

bool FindBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);  // NOLINT(*-static-cast-downcast)
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if ((key->modifiers() & Qt::ShiftModifier) != 0) {
                findPrevious();
            } else {
                findNext();
            }
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace lv::ui
