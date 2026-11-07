#pragma once

#include "core/search/query.h"
#include "model/line_finder.h"

#include <QWidget>

#include <memory>
#include <optional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace lv::model
{
class Document;
class FilteredDocument;
class MatchHighlighter;
}  // namespace lv::model

namespace lv::ui
{

class LogView;

// Inline find bar of a tab (SPEC F-FIND-1 … F-FIND-4). It searches the view that had focus when
// it was opened (main or filtered) for the next or previous matching line, starting after or
// before the current line and wrapping around with a notice. The search runs in the background
// through the whole file (filtered view: through the matches of the filter). All matches in
// visible lines of both views are highlighted while the bar is open; an invalid regex is shown
// inline and no search runs.
//
// Thread-safety: UI thread only.
class FindBar : public QWidget
{
    Q_OBJECT

public:
    FindBar(model::Document& document, model::FilteredDocument& filter, LogView& mainView,
            LogView& filteredView, QWidget* parent = nullptr);
    FindBar(const FindBar&) = delete;
    FindBar& operator=(const FindBar&) = delete;
    FindBar(FindBar&&) = delete;
    FindBar& operator=(FindBar&&) = delete;
    ~FindBar() override;

    // Shows the bar for `target`, focuses the field, and selects its text.
    void open(LogView* target);
    // Hides the bar, stops a running search, removes the highlights, and focuses the target.
    void close();

    void findNext();
    void findPrevious();

    [[nodiscard]] LogView* target() const noexcept { return m_target; }

    [[nodiscard]] bool isSearching() const noexcept { return m_finder->isRunning(); }

    [[nodiscard]] const model::MatchHighlighter& highlighter() const noexcept
    {
        return *m_highlighter;
    }

    [[nodiscard]] QLineEdit* input() const noexcept { return m_input; }

    [[nodiscard]] QCheckBox* caseSensitiveBox() const noexcept { return m_caseSensitive; }

    [[nodiscard]] QCheckBox* wholeWordBox() const noexcept { return m_wholeWord; }

    [[nodiscard]] QCheckBox* regexBox() const noexcept { return m_regex; }

    [[nodiscard]] QLabel* statusLabel() const noexcept { return m_status; }

    [[nodiscard]] QLabel* errorLabel() const noexcept { return m_error; }

signals:
    // A search finished (found, not found, or failed); the status label is up to date.
    void searchFinished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] core::Query enteredQuery() const;
    // Compiles the entered query; shows or hides the error. Null when empty or invalid.
    std::shared_ptr<const core::CompiledQuery> compile();
    void updateHighlight();
    void find(model::FindDirection direction);
    void showResult(const model::FindResult& result);

    model::Document* m_document;
    model::FilteredDocument* m_filter;
    LogView* m_mainView;
    LogView* m_filteredView;
    LogView* m_target;
    std::unique_ptr<model::MatchHighlighter> m_highlighter;
    model::LineFinder* m_finder;
    std::shared_ptr<const core::CompiledQuery> m_searching;  // query of the running search
    LogView* m_searchView = nullptr;
    model::FindDirection m_searchDirection = model::FindDirection::kForward;

    QLineEdit* m_input;
    QCheckBox* m_caseSensitive;
    QCheckBox* m_wholeWord;
    QCheckBox* m_regex;
    QPushButton* m_previous;
    QPushButton* m_next;
    QPushButton* m_close;
    QLabel* m_status;
    QLabel* m_error;
};

}  // namespace lv::ui
