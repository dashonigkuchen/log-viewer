#pragma once

#include <QString>
#include <QWidget>

#include <filesystem>
#include <memory>

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
};

// Absolute, lexically normalized form of `path` (also resolves symlinks when the file exists).
[[nodiscard]] std::filesystem::path normalizedPath(const std::filesystem::path& path);

}  // namespace lv::ui
