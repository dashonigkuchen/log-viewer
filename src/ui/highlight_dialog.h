#pragma once

#include "model/highlight_sets.h"

#include <QDialog>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace lv::model
{
class LineHighlighter;
class StaticLineSource;
}  // namespace lv::model

namespace lv::ui
{

class LogView;

// Edits highlight sets and their rules (SPEC F-HL-4): create, rename, duplicate, delete, and make
// sets the default; add, edit, enable/disable, duplicate, delete, and reorder rules (drag and
// drop or Move Up/Down); import and export sets as JSON (F-HL-6). Works on a copy; a preview of
// sample lines shows the selected set while editing. OK hands the result to the caller.
//
// Thread-safety: UI thread only.
class HighlightDialog : public QDialog
{
    Q_OBJECT

public:
    HighlightDialog(const model::HighlightSets& sets, QStringList previewLines,
                    QWidget* parent = nullptr);
    ~HighlightDialog() override;
    HighlightDialog(const HighlightDialog&) = delete;
    HighlightDialog& operator=(const HighlightDialog&) = delete;
    HighlightDialog(HighlightDialog&&) = delete;
    HighlightDialog& operator=(HighlightDialog&&) = delete;

    // The edited sets and default (valid after OK, or any time for tests).
    [[nodiscard]] const std::vector<model::HighlightSet>& sets() const noexcept { return m_sets; }

    [[nodiscard]] std::size_t defaultIndex() const noexcept { return m_default; }

    // Selects set `index` and rule `rule` (if any) in the lists.
    void selectSet(std::size_t index);
    void selectRule(int rule);

    // File dialogs, replaceable for tests: return a path or an empty string.
    void setFileChoosers(std::function<QString()> openFile, std::function<QString()> saveFile);

    // Widgets for tests.
    [[nodiscard]] QListWidget* setList() const noexcept { return m_setList; }

    [[nodiscard]] QListWidget* ruleList() const noexcept { return m_ruleList; }

    [[nodiscard]] QLineEdit* patternEdit() const noexcept { return m_pattern; }

    [[nodiscard]] QCheckBox* regexBox() const noexcept { return m_regex; }

    [[nodiscard]] QCheckBox* caseBox() const noexcept { return m_caseSensitive; }

    [[nodiscard]] QComboBox* scopeBox() const noexcept { return m_scope; }

    [[nodiscard]] QLineEdit* foregroundEdit() const noexcept { return m_foreground; }

    [[nodiscard]] QLineEdit* backgroundEdit() const noexcept { return m_background; }

    [[nodiscard]] QLabel* errorLabel() const noexcept { return m_error; }

    [[nodiscard]] LogView* preview() const noexcept { return m_preview; }

    // Actions (also bound to the buttons).
    void addSet();
    void duplicateSet();
    void deleteSet();
    void makeDefault();
    void addRule();
    void duplicateRule();
    void deleteRule();
    void moveRule(int delta);
    void importSets();
    void exportSets();

    // Parses "#RRGGBB" / "#AARRGGBB"; empty = no color. nullopt in the outer optional = invalid.
    [[nodiscard]] static std::optional<std::optional<model::Argb>> parseColor(const QString& text);
    [[nodiscard]] static QString formatColor(std::optional<model::Argb> color);

private:
    [[nodiscard]] std::optional<std::size_t> currentSet() const;
    [[nodiscard]] int currentRule() const;
    [[nodiscard]] model::HighlightSet* editedSet();
    void rebuildSetList();
    void rebuildRuleList();
    void showRule();
    void storeRule();
    void onRulesMoved();
    void onRuleItemChanged(QListWidgetItem* item);
    void onSetItemChanged(QListWidgetItem* item);
    void updatePreview();
    void pickColor(QLineEdit* edit);
    [[nodiscard]] static QString ruleLabel(const model::HighlightRule& rule);
    void buildLayout();

    std::vector<model::HighlightSet> m_sets;
    std::size_t m_default = 0;
    bool m_updating = false;
    std::function<QString()> m_openFile;
    std::function<QString()> m_saveFile;

    QListWidget* m_setList = nullptr;
    QListWidget* m_ruleList = nullptr;
    QLineEdit* m_pattern = nullptr;
    QCheckBox* m_regex = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QComboBox* m_scope = nullptr;
    QLineEdit* m_foreground = nullptr;
    QLineEdit* m_background = nullptr;
    QLabel* m_error = nullptr;
    QWidget* m_ruleEditor = nullptr;
    LogView* m_preview = nullptr;

    // The preview shows the selected set through its own copy of the sets.
    std::unique_ptr<model::HighlightSets> m_previewSets;
    std::unique_ptr<model::LineHighlighter> m_previewHighlighter;
    std::unique_ptr<model::StaticLineSource> m_previewSource;
};

}  // namespace lv::ui
