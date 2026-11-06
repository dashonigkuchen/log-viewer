#pragma once

#include "core/encoding/encoding.h"
#include "core/util/strong_types.h"

#include <QString>
#include <QWidget>

#include <filesystem>
#include <memory>
#include <optional>

namespace lv::model
{
class Document;
class DocumentLineSource;
}  // namespace lv::model

namespace lv::ui
{

class LogView;

// One opened file in the main window (ARCHITECTURE §6): owns the Document, its line source, and
// the main LogView. The filtered view and filter bar join in M3.
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

private:
    std::filesystem::path m_path;
    std::unique_ptr<model::Document> m_document;
    std::unique_ptr<model::DocumentLineSource> m_source;
    LogView* m_view = nullptr;
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
