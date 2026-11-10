#include "ui/settings_dialog.h"

#include "model/settings.h"
#include "ui/highlight_dialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lv::ui
{
namespace
{

QString argbText(std::uint32_t color)
{
    return QStringLiteral("#%1").arg(color, 8, 16, QChar(u'0')).toUpper();
}

QSpinBox* spinBox(int min, int max, int value, const QString& suffix, QWidget* parent)
{
    auto* box = new QSpinBox(parent);
    box->setRange(min, max);
    box->setValue(value);
    box->setSuffix(suffix);
    return box;
}

}  // namespace

SettingsDialog::SettingsDialog(model::Settings& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(&settings)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(QStringLiteral("Settings"));

    // View.
    auto* view = new QGroupBox(QStringLiteral("View"), this);
    m_systemFont = new QCheckBox(QStringLiteral("System fixed-pitch font"), view);
    m_systemFont->setChecked(settings.fontFamily().isEmpty());
    m_font = new QFontComboBox(view);
    m_font->setFontFilters(QFontComboBox::MonospacedFonts);
    m_font->setCurrentFont(settings.fontFamily().isEmpty()
                               ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                               : QFont(settings.fontFamily()));
    m_font->setEnabled(!m_systemFont->isChecked());
    connect(m_systemFont, &QCheckBox::toggled, m_font,
            [this](bool system) { m_font->setEnabled(!system); });
    m_fontSize = spinBox(model::Settings::kMinFontSize, model::Settings::kMaxFontSize,
                         settings.fontSize(), QStringLiteral(" pt"), view);
    m_tabWidth = spinBox(1, 32, settings.tabWidth(), QString(), view);
    m_lineNumbers = new QCheckBox(QStringLiteral("Show line numbers"), view);
    m_lineNumbers->setChecked(settings.lineNumbersVisible());
    m_columns = spinBox(model::Settings::kMinMaxColumns, model::Settings::kMaxMaxColumns,
                        settings.maxColumns(), QStringLiteral(" characters"), view);
    m_columns->setToolTip(QStringLiteral("Longer lines are cut for display (copy keeps them)."));
    auto* viewForm = new QFormLayout(view);
    viewForm->addRow(QString(), m_systemFont);
    viewForm->addRow(QStringLiteral("Font:"), m_font);
    viewForm->addRow(QStringLiteral("Font size:"), m_fontSize);
    viewForm->addRow(QStringLiteral("Tab width:"), m_tabWidth);
    viewForm->addRow(QString(), m_lineNumbers);
    viewForm->addRow(QStringLiteral("Max line length:"), m_columns);

    // Colors.
    auto* colors = new QGroupBox(QStringLiteral("Colors (#RRGGBB or #AARRGGBB)"), this);
    const auto colorRow = [this, colors](QLineEdit*& edit, const QString& text,
                                         const QString& placeholder) {
        edit = new QLineEdit(text, colors);
        edit->setPlaceholderText(placeholder);
        auto* row = new QHBoxLayout;
        row->addWidget(edit);
        auto* pick = new QPushButton(QStringLiteral("..."), colors);
        pick->setAutoDefault(false);
        pick->setMaximumWidth(32);
        QLineEdit* target = edit;
        connect(pick, &QPushButton::clicked, this, [this, target] {
            const auto parsed = HighlightDialog::parseColor(target->text());
            const QColor initial =
                parsed && *parsed ? QColor::fromRgba(**parsed) : QColor(Qt::white);
            const QColor color =
                QColorDialog::getColor(initial, this, QString(), QColorDialog::ShowAlphaChannel);
            if (color.isValid()) {
                target->setText(argbText(color.rgba()));
            }
        });
        row->addWidget(pick);
        return row;
    };
    const auto text = [](std::optional<std::uint32_t> color) {
        return color.has_value() ? argbText(*color) : QString();
    };
    const QString themeDefault = QStringLiteral("empty = default for the theme");
    m_scheme = new QComboBox(colors);
    m_scheme->addItem(QStringLiteral("System"));
    m_scheme->addItem(QStringLiteral("Light"));
    m_scheme->addItem(QStringLiteral("Dark"));
    m_scheme->setCurrentIndex(static_cast<int>(settings.colorScheme()));
    auto* colorForm = new QFormLayout(colors);
    colorForm->addRow(QStringLiteral("Theme:"), m_scheme);
    colorForm->addRow(QStringLiteral("Current line:"),
                      colorRow(m_currentLine, text(settings.currentLineColor()), themeDefault));
    colorForm->addRow(QStringLiteral("Marks:"),
                      colorRow(m_mark, text(settings.markColor()), themeDefault));
    colorForm->addRow(QStringLiteral("Filter matches:"),
                      colorRow(m_match, text(settings.matchColor()), themeDefault));

    // Behavior.
    auto* behavior = new QGroupBox(QStringLiteral("Behavior"), this);
    m_poll = spinBox(model::Settings::kMinPollMs, model::Settings::kMaxPollMs,
                     settings.pollInterval(), QStringLiteral(" ms"), behavior);
    m_poll->setSingleStep(100);
    m_poll->setToolTip(QStringLiteral("How often open files are checked for changes."));
    m_history = spinBox(1, model::Settings::kMaxHistorySize, settings.filterHistorySize(),
                        QStringLiteral(" entries"), behavior);
    m_session = new QCheckBox(QStringLiteral("Restore open files at startup"), behavior);
    m_session->setChecked(settings.restoreSession());
    m_singleInstance = new QCheckBox(QStringLiteral("Open files in the running window"), behavior);
    m_singleInstance->setChecked(settings.singleInstance());
    m_threads = spinBox(0, model::Settings::kMaxSearchThreads, settings.searchThreads(), QString(),
                        behavior);
    m_threads->setSpecialValueText(QStringLiteral("Automatic"));
    m_threads->setToolTip(QStringLiteral("Takes effect after a restart."));
    auto* behaviorForm = new QFormLayout(behavior);
    behaviorForm->addRow(QStringLiteral("Check files every:"), m_poll);
    behaviorForm->addRow(QStringLiteral("Filter history:"), m_history);
    behaviorForm->addRow(QStringLiteral("Search threads:"), m_threads);
    behaviorForm->addRow(QString(), m_session);
    behaviorForm->addRow(QString(), m_singleInstance);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(view);
    layout->addWidget(colors);
    layout->addWidget(behavior);
    layout->addWidget(buttons);
}

bool SettingsDialog::apply()
{
    const auto currentLine = HighlightDialog::parseColor(m_currentLine->text());
    const auto mark = HighlightDialog::parseColor(m_mark->text());
    const auto match = HighlightDialog::parseColor(m_match->text());
    if (!currentLine || !mark || !match) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("Please enter colors as #RRGGBB or #AARRGGBB."));
        return false;
    }
    m_settings->setFont(m_systemFont->isChecked() ? QString() : m_font->currentFont().family(),
                        m_fontSize->value());
    m_settings->setTabWidth(m_tabWidth->value());
    m_settings->setLineNumbersVisible(m_lineNumbers->isChecked());
    m_settings->setMaxColumns(m_columns->value());
    m_settings->setCurrentLineColor(*currentLine);
    m_settings->setMarkColor(*mark);
    m_settings->setMatchColor(*match);
    m_settings->setColorScheme(static_cast<model::ColorScheme>(m_scheme->currentIndex()));
    m_settings->setPollInterval(m_poll->value());
    m_settings->setFilterHistorySize(m_history->value());
    m_settings->setRestoreSession(m_session->isChecked());
    m_settings->setSingleInstance(m_singleInstance->isChecked());
    m_settings->setSearchThreads(m_threads->value());
    m_settings->sync();
    return true;
}

void SettingsDialog::accept()
{
    if (apply()) {
        QDialog::accept();
    }
}

}  // namespace lv::ui
