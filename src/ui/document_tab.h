#pragma once

#include "core/encoding/encoding.h"
#include "core/util/strong_types.h"

#include <QString>
#include <QWidget>

class QLabel;
class QSplitter;
class QTimer;
class QWidget;

#include <filesystem>
#include <memory>
#include <optional>

namespace lv::model
{
class Document;
class DocumentLineSource;
class FilteredDocument;
class FilteredLineSource;
class AnnotationStore;
struct SessionTab;
class Bookmarks;
class LineHighlighter;
class MarkSet;
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

    [[nodiscard]] model::MarkSet& marks() const noexcept { return *m_marks; }

    [[nodiscard]] model::Bookmarks& bookmarks() const noexcept { return *m_bookmarks; }

    // Highlight rules of this tab (F-HL-3, F-HL-5).
    [[nodiscard]] model::LineHighlighter& lineHighlighter() const noexcept
    {
        return *m_lineHighlighter;
    }

    // Where marks and bookmarks of this file are persisted (F-PERS-1); null = not persisted.
    // Set before the document is ready to restore saved annotations. Not owned.
    void setAnnotationStore(model::AnnotationStore* store);
    // Writes the current marks and bookmarks to the store now.
    void saveAnnotations();

    // Position, filter, and follow mode for session restore (F-FILE-10).
    [[nodiscard]] model::SessionTab sessionState() const;
    // Applies a saved state: the filter at once, the position when the document is ready.
    void restoreSessionState(const model::SessionTab& state);

    // The view that has (or last had) the keyboard focus; the main view by default.
    [[nodiscard]] LogView* activeView() const;
    // Shows `line` in the main view (and the filtered view, if it shows the line).
    void goToLine(core::LineNumber line);

    // Marked lines also appear in the filtered view (F-FILT-9; default on).
    void setShowMarksInFilter(bool show);
    [[nodiscard]] bool showMarksInFilter() const;

    [[nodiscard]] model::FilteredLineSource& filteredSource() const noexcept
    {
        return *m_filteredSource;
    }

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

    // Status bar text: size, lines, encoding, current line, indexing progress, follow mode
    // (F-TAB-6).
    [[nodiscard]] QString statusText() const;

    // Follow mode of both views (F-TAIL-3, per tab; off by default).
    void setFollowing(bool following);

    [[nodiscard]] bool isFollowing() const noexcept { return m_following; }

    // Non-modal notice above the views (truncated, rotated, deleted; F-TAIL-5/6/7); hidden when
    // empty. Closed by its button.
    [[nodiscard]] QWidget* noticeBar() const noexcept { return m_noticeBar; }

    [[nodiscard]] QString noticeText() const;
    void showNotice(const QString& message);
    void hideNotice();

signals:
    // statusText() or the indexing state changed.
    void statusChanged();
    // The user moved the splitter between main and filtered view.
    void splitterMoved();
    void followingChanged(bool following);
    // New lines arrived or the file was reloaded (for the inactive-tab indicator, F-TAB-3).
    void contentChanged();
    // "Highlight this text" in one of the views (F-HL-7).
    void highlightTextRequested(const QString& text);

private:
    std::filesystem::path m_path;
    std::unique_ptr<model::Document> m_document;
    std::unique_ptr<model::DocumentLineSource> m_source;
    std::unique_ptr<model::FilteredDocument> m_filter;
    std::unique_ptr<model::FilteredLineSource> m_filteredSource;
    std::unique_ptr<model::MatchHighlighter> m_highlighter;
    std::unique_ptr<model::MarkSet> m_marks;
    std::unique_ptr<model::Bookmarks> m_bookmarks;
    std::unique_ptr<model::LineHighlighter> m_lineHighlighter;
    QTimer* m_annotationTimer = nullptr;  // saves annotations after changes
    QSplitter* m_splitter = nullptr;
    LogView* m_view = nullptr;
    LogView* m_filteredView = nullptr;
    FilterBar* m_filterBar = nullptr;
    FindBar* m_findBar = nullptr;
    QWidget* m_noticeBar = nullptr;
    QLabel* m_noticeLabel = nullptr;
    bool m_syncing = false;  // guards the two-way view synchronization
    bool m_following = false;

    void syncMainToFiltered(core::LineNumber line);
    void syncFilteredToMain(core::LineNumber row);
    quint64 m_indexedBytes = 0;
    quint64 m_totalBytes = 0;

    // Position to restore once a reload has finished.
    void rememberPosition();
    void restorePosition();
    std::optional<core::LineNumber> m_restoreTop;
    std::optional<core::LineNumber> m_restoreCurrent;
    void restoreAnnotations();
    model::AnnotationStore* m_annotationStore = nullptr;
    bool m_annotationsRestored = false;
    bool m_annotationsDirty = false;
};

// Absolute, lexically normalized form of `path` (also resolves symlinks when the file exists).
[[nodiscard]] std::filesystem::path normalizedPath(const std::filesystem::path& path);

}  // namespace lv::ui
