#include "ui/go_to_line_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lv::ui
{

GoToLineDialog::GoToLineDialog(std::uint64_t lineCount, std::uint64_t currentLine, QWidget* parent)
    : QDialog(parent)
    , m_lineCount(lineCount)
{
    setWindowTitle(QStringLiteral("Go to Line"));
    auto* layout = new QVBoxLayout(this);
    m_hint = new QLabel(QStringLiteral("Line number (1 - %1):").arg(lineCount), this);
    m_edit = new QLineEdit(QString::number(currentLine), this);
    m_edit->setObjectName(QStringLiteral("lineEdit"));
    m_edit->selectAll();
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(m_hint);
    layout->addWidget(m_edit);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_edit, &QLineEdit::textChanged, this, &GoToLineDialog::updateState);
    updateState();
}

std::optional<std::uint64_t> GoToLineDialog::parseLine(const QString& text, std::uint64_t lineCount)
{
    bool ok = false;
    const qulonglong value = text.trimmed().toULongLong(&ok);
    if (!ok || value == 0 || value > lineCount) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint64_t> GoToLineDialog::line() const
{
    return parseLine(m_edit->text(), m_lineCount);
}

void GoToLineDialog::updateState()
{
    m_ok->setEnabled(line().has_value());
}

}  // namespace lv::ui
