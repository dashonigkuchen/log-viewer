#pragma once

#include "model/bookmarks.h"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace lv::model
{

class MarkSet;

// Marks and bookmarks of one file, as stored.
struct FileAnnotations
{
    struct Mark
    {
        std::uint64_t line = 0;
        int color = 0;

        friend bool operator==(const Mark&, const Mark&) = default;
    };

    struct Bookmark
    {
        std::uint64_t line = 0;
        QString name;

        friend bool operator==(const Bookmark&, const Bookmark&) = default;
    };

    std::vector<Mark> marks;
    std::vector<Bookmark> bookmarks;

    [[nodiscard]] bool empty() const noexcept { return marks.empty() && bookmarks.empty(); }

    [[nodiscard]] static FileAnnotations of(const MarkSet& markSet, const Bookmarks& bookmarkSet);
    // Applies to empty sets; entries at or after `lineCount` are dropped. Returns how many marks
    // and bookmarks were restored.
    std::size_t applyTo(MarkSet& markSet, Bookmarks& bookmarkSet, std::uint64_t lineCount) const;

    friend bool operator==(const FileAnnotations&, const FileAnnotations&) = default;
};

// Fingerprint of the start of a file for F-PERS-1: FNV-1a over the first kBytes bytes (fewer if
// the file is shorter) plus that length. nullopt if the file cannot be read.
struct FileKey
{
    static constexpr std::size_t kBytes = std::size_t{64} * 1024;

    std::uint64_t length = 0;
    std::uint64_t hash = 0;

    [[nodiscard]] static std::optional<FileKey> read(const QString& path);

    friend bool operator==(const FileKey&, const FileKey&) = default;
};

// Persists marks and bookmarks per file (SPEC F-PERS-1) in a JSON file in the settings
// directory, keyed by absolute path; each entry stores the FileKey of the file it belongs to.
// At most kLimit files are kept (least recently saved are dropped).
//
// Thread-safety: UI thread only.
class AnnotationStore
{
public:
    static constexpr std::size_t kLimit = 500;

    enum class Lookup : std::uint8_t
    {
        kNone,      // nothing stored for this path
        kFound,     // stored and the fingerprint matches
        kMismatch,  // stored, but the file changed: the entry was discarded
    };

    struct Result
    {
        Lookup lookup = Lookup::kNone;
        FileAnnotations annotations;
    };

    // `file` is the JSON file (created on the first save).
    explicit AnnotationStore(QString file);

    // Looks up `path` with the file's current key; a mismatching entry is removed.
    [[nodiscard]] Result load(const QString& path, const FileKey& key);
    // Stores (or, if empty, removes) the annotations of `path`, marking it most recently used.
    void save(const QString& path, const FileKey& key, const FileAnnotations& annotations);

    [[nodiscard]] std::size_t size();

private:
    struct Entry
    {
        QString path;
        FileKey key;
        FileAnnotations annotations;
    };

    void ensureLoaded();
    void write() const;

    QString m_file;
    bool m_loaded = false;
    std::vector<Entry> m_entries;  // most recently saved first
};

}  // namespace lv::model
