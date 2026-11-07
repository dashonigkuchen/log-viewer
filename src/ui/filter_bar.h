#pragma once

#include "core/search/query.h"

#include <QWidget>

#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace lv::model
{
class FilteredDocument;
}

namespace lv::ui
{

// The filter input above the filtered view (SPEC F-FILT-2, F-FILT-5, F-FILT-6, F-FILT-10):
// text field, mode selector (Literal / Regex / Boolean), options (case sensitive, whole word,
// invert), Clear, and a status line with progress, match count, and skipped lines.
//
// The filter is applied only on Enter. Esc restores the last applied filter. An invalid query
// is reported inline with its position; the previous result stays.
//
// Thread-safety: UI thread only.
class FilterBar : public QWidget
{
    Q_OBJECT

public:
    explicit FilterBar(model::FilteredDocument& filter, QWidget* parent = nullptr);

    // The query currently entered (not necessarily applied).
    [[nodiscard]] core::Query enteredQuery() const;

    // Applies the entered query (Enter). Returns false and shows the error if it is invalid.
    bool apply();

    // Restores the field and options of the last applied filter (Esc).
    void restore();

    // Removes the filter (Clear).
    void clear();

    [[nodiscard]] QLineEdit* input() const noexcept { return m_input; }

    [[nodiscard]] QComboBox* modeSelector() const noexcept { return m_mode; }

    [[nodiscard]] QCheckBox* caseSensitiveBox() const noexcept { return m_caseSensitive; }

    [[nodiscard]] QCheckBox* wholeWordBox() const noexcept { return m_wholeWord; }

    [[nodiscard]] QCheckBox* invertBox() const noexcept { return m_invert; }

    [[nodiscard]] QLabel* statusLabel() const noexcept { return m_status; }

    [[nodiscard]] QLabel* errorLabel() const noexcept { return m_error; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showQuery(const std::optional<core::Query>& query);
    void showError(const QString& message, std::size_t position);
    void hideError();
    void updateOptions();
    void updateStatus();

    model::FilteredDocument* m_filter;
    QLineEdit* m_input = nullptr;
    QComboBox* m_mode = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QCheckBox* m_wholeWord = nullptr;
    QCheckBox* m_invert = nullptr;
    QPushButton* m_clear = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_error = nullptr;
};

}  // namespace lv::ui
