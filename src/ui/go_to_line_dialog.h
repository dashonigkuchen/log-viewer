#pragma once

#include <QDialog>
#include <QString>

#include <cstdint>
#include <optional>

class QLabel;
class QLineEdit;
class QPushButton;

namespace lv::ui
{

// "Go to line" dialog (Ctrl+G, SPEC F-VIEW-6). Accepts a 1-based line number between 1 and the
// line count; OK is disabled for anything else.
//
// Thread-safety: UI thread only.
class GoToLineDialog : public QDialog
{
    Q_OBJECT

public:
    GoToLineDialog(std::uint64_t lineCount, std::uint64_t currentLine, QWidget* parent = nullptr);

    // Parses a 1-based line number ("  42 " -> 42); nullopt if it is not in [1, lineCount].
    [[nodiscard]] static std::optional<std::uint64_t> parseLine(const QString& text,
                                                                std::uint64_t lineCount);

    // The entered 1-based line number, if valid.
    [[nodiscard]] std::optional<std::uint64_t> line() const;

    [[nodiscard]] QLineEdit* lineEdit() const noexcept { return m_edit; }

    [[nodiscard]] QPushButton* okButton() const noexcept { return m_ok; }

private:
    void updateState();

    std::uint64_t m_lineCount;
    QLineEdit* m_edit = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_ok = nullptr;
};

}  // namespace lv::ui
