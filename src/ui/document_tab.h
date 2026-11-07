#pragma once

#include "core/encoding/encoding.h"
#include "core/util/strong_types.h"

#include <QString>
#include <QWidget>

class QSplitter;

#include <filesystem>
#include <memory>
#include <optional>

namespace lv::model
{
class Document;
class DocumentLineSource;
class FilteredDocument;
class FilteredLineSource;
class MatchHighlighter;
}  // namespace lv::model

namespace lv::ui
{

class FilterBar;
class FindBar;
class LogView;

// One opened file in the main window (ARCHITECTURE §6): owns the Document and its filter, and
// shows the main view above the filtered view in a splitter (F-FILT-1). Both views follow each
// other (F-FILT-7): making a filtered row current moves the main view to its line; moving in
// the main view selects the nearest match at or before the current line. The filter bar sits
// above the filtered view (pane `filterPane`).
//
// Thread-safety: UI thread only.
class DocumentTab : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentTab(const std::filesystem::path& path, QWidget* parent = nullptr);
    DocumentTab(const DocumentTab&) = delete;
    DocumentTab& operator=(const DocumentTab&) = delete;
    DocumentTab(DocumentTab&&) = delete;
    DocumentTab& operator=(DocumentTab&&) = delete;
    ~DocumentTab() override;

    [[nodiscard]] model::Document& document() const noexcept { return *m_document; }

    [[nodiscard]] LogView* mainView() const noexcept { return m_view; }

    [[nodiscard]] LogView* filteredView() const noexcept { return m_filteredView; }

    [[nodiscard]] FilterBar* filterBar() const noexcept { return m_filterBar; }

    [[nodiscard]] FindBar* findBar() const noexcept { return m_findBar; }

    // Opens the find bar for the view that has focus (main view otherwise; Ctrl+F, F-FIND-1).
    void openFindBar();

    [[nodiscard]] model::FilteredDocument& filter() const noexcept { return *m_filter; }

    [[nodiscard]] const model::MatchHighlighter& highlighter() const noexcept
    {
        return *m_highlighter;
    }

    [[nodiscard]] QSplitter* splitter() const noexcept { return m_splitter; }

    // Absolute, normalized path used to detect files that are already open.
    [[nodiscard]] const std::filesystem::path& canonicalPath() const noexcept { return m_path; }

    [[nodiscard]] QString title() const;    // file name
    [[nodiscard]] QString toolTip() const;  // full native path

    // Re-indexes the file and restores the top and current line afterwards (F-FILE-8).
    void reload();

    // Overrides the encoding (nullopt = detect) and re-indexes, keeping the position (F-ENC-3).
    void setEncodingOverride(std::optional<core::Encoding> encoding);

    // Status bar text: size, lines, encoding, current line, indexing progress (F-TAB-6).
    [[nodiscard]] QString statusText() const;

signals:
    // statusText() or the indexing state changed.
    void statusChanged();
    // The user moved the splitter between main and filtered view.
    void splitterMoved();

private:
    std::filesystem::path m_path;
    std::unique_ptr<model::Document> m_document;
    std::unique_ptr<model::DocumentLineSource> m_source;
    std::unique_ptr<model::FilteredDocument> m_filter;
    std::unique_ptr<model::FilteredLineSource> m_filteredSource;
    std::unique_ptr<model::MatchHighlighter> m_highlighter;
    QSplitter* m_splitter = nullptr;
    LogView* m_view = nullptr;
    LogView* m_filteredView = nullptr;
    FilterBar* m_filterBar = nullptr;
    FindBar* m_findBar = nullptr;
    bool m_syncing = false;  // guards the two-way view synchronization

    void syncMainToFiltered(core::LineNumber line);
    void syncFilteredToMain(core::LineNumber row);
    quint64 m_indexedBytes = 0;
    quint64 m_totalBytes = 0;

    // Position to restore once a reload has finished.
    void rememberPosition();
    void restorePosition();
    std::optional<core::LineNumber> m_restoreTop;
    std::optional<core::LineNumber> m_restoreCurrent;
};

// Absolute, lexically normalized form of `path` (also resolves symlinks when the file exists).
[[nodiscard]] std::filesystem::path normalizedPath(const std::filesystem::path& path);

}  // namespace lv::ui
