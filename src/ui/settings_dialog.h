#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace lv::model
{
class FilterHistory;
class Settings;
}  // namespace lv::model

namespace lv::ui
{

// Edits the application settings (SPEC F-SET-1): font, tab width, line numbers, colors (current
// line, marks, match highlight), polling interval, max display columns, filter history size,
// session restore, single instance, and search threads. OK writes the values; changes take
// effect immediately except the thread count (next start).
//
// Thread-safety: UI thread only.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // `history` enables "Clear Filter History" (F-HIST-3); it may be null.
    explicit SettingsDialog(model::Settings& settings, model::FilterHistory* history = nullptr,
                            QWidget* parent = nullptr);

    // Writes the values to the settings (called on OK; public for tests). Returns false and
    // shows the problem if a color is invalid.
    bool apply();

    // Widgets for tests.
    [[nodiscard]] QFontComboBox* fontBox() const noexcept { return m_font; }

    [[nodiscard]] QSpinBox* fontSizeBox() const noexcept { return m_fontSize; }

    [[nodiscard]] QSpinBox* tabWidthBox() const noexcept { return m_tabWidth; }

    [[nodiscard]] QCheckBox* lineNumbersBox() const noexcept { return m_lineNumbers; }

    [[nodiscard]] QComboBox* schemeBox() const noexcept { return m_scheme; }

    [[nodiscard]] QLineEdit* currentLineEdit() const noexcept { return m_currentLine; }

    [[nodiscard]] QLineEdit* markEdit() const noexcept { return m_mark; }

    [[nodiscard]] QLineEdit* matchEdit() const noexcept { return m_match; }

    [[nodiscard]] QSpinBox* pollBox() const noexcept { return m_poll; }

    [[nodiscard]] QSpinBox* columnsBox() const noexcept { return m_columns; }

    [[nodiscard]] QSpinBox* historyBox() const noexcept { return m_history; }

    [[nodiscard]] QCheckBox* sessionBox() const noexcept { return m_session; }

    [[nodiscard]] QCheckBox* singleInstanceBox() const noexcept { return m_singleInstance; }

    [[nodiscard]] QSpinBox* threadsBox() const noexcept { return m_threads; }

    // Clears the filter history at once (not on OK); disabled without a history.
    [[nodiscard]] QPushButton* clearHistoryButton() const noexcept { return m_clearHistory; }

private:
    void accept() override;

    QPushButton* m_clearHistory = nullptr;
    model::Settings* m_settings;
    QFontComboBox* m_font = nullptr;
    QCheckBox* m_systemFont = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QSpinBox* m_tabWidth = nullptr;
    QCheckBox* m_lineNumbers = nullptr;
    QComboBox* m_scheme = nullptr;
    QLineEdit* m_currentLine = nullptr;
    QLineEdit* m_mark = nullptr;
    QLineEdit* m_match = nullptr;
    QSpinBox* m_poll = nullptr;
    QSpinBox* m_columns = nullptr;
    QSpinBox* m_history = nullptr;
    QCheckBox* m_session = nullptr;
    QCheckBox* m_singleInstance = nullptr;
    QSpinBox* m_threads = nullptr;
};

}  // namespace lv::ui
