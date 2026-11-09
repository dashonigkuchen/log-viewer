#include "model/annotation_store.h"

#include "core/util/log.h"
#include "model/mark_set.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace lv::model
{
namespace
{

constexpr auto kFormat = "logviewer-annotations";
constexpr int kVersion = 1;

QJsonObject toJson(const FileAnnotations& annotations)
{
    QJsonArray marks;
    for (const auto& mark : annotations.marks) {
        marks.append(QJsonArray{static_cast<qint64>(mark.line), mark.color});
    }
    QJsonArray bookmarks;
    for (const auto& bookmark : annotations.bookmarks) {
        bookmarks.append(QJsonObject{{QStringLiteral("line"), static_cast<qint64>(bookmark.line)},
                                     {QStringLiteral("name"), bookmark.name}});
    }
    return {{QStringLiteral("marks"), marks}, {QStringLiteral("bookmarks"), bookmarks}};
}

FileAnnotations annotationsFromJson(const QJsonObject& object)
{
    FileAnnotations out;
    for (const auto& value : object.value(QStringLiteral("marks")).toArray()) {
        const QJsonArray pair = value.toArray();
        const qint64 line = pair.at(0).toInteger(-1);
        const int color = pair.at(1).toInt(0);
        if (line >= 0 && color >= 0 && color < MarkSet::kColorCount) {
            out.marks.push_back({.line = static_cast<std::uint64_t>(line), .color = color});
        }
    }
    for (const auto& value : object.value(QStringLiteral("bookmarks")).toArray()) {
        const QJsonObject bookmark = value.toObject();
        const qint64 line = bookmark.value(QStringLiteral("line")).toInteger(-1);
        if (line >= 0) {
            out.bookmarks.push_back({.line = static_cast<std::uint64_t>(line),
                                     .name = bookmark.value(QStringLiteral("name")).toString()});
        }
    }
    return out;
}

}  // namespace

FileAnnotations FileAnnotations::of(const MarkSet& markSet, const Bookmarks& bookmarkSet)
{
    FileAnnotations out;
    for (const core::LineNumber line : markSet.lines()) {
        out.marks.push_back({.line = line.get(), .color = markSet.colorOf(line).value_or(0)});
    }
    for (const auto& entry : bookmarkSet.entries()) {
        out.bookmarks.push_back({.line = entry.line.get(), .name = entry.name});
    }
    return out;
}

std::size_t FileAnnotations::applyTo(MarkSet& markSet, Bookmarks& bookmarkSet,
                                     std::uint64_t lineCount) const
{
    std::size_t restored = 0;
    for (const auto& mark : marks) {
        if (mark.line < lineCount) {
            const core::LineNumber line(mark.line);
            markSet.toggleColor(std::span(&line, 1), mark.color);
            ++restored;
        }
    }
    for (const auto& bookmark : bookmarks) {
        if (bookmark.line < lineCount) {
            bookmarkSet.add(core::LineNumber(bookmark.line), bookmark.name);
            ++restored;
        }
    }
    return restored;
}

std::optional<FileKey> FileKey::read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray head = file.read(static_cast<qint64>(kBytes));
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffset;
    for (const char c : head) {
        hash ^= static_cast<unsigned char>(c);
        hash *= kPrime;
    }
    return FileKey{.length = static_cast<std::uint64_t>(head.size()), .hash = hash};
}

AnnotationStore::AnnotationStore(QString file)
    : m_file(std::move(file))
{}

void AnnotationStore::ensureLoaded()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly)) {
        return;  // nothing stored yet
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QString::fromLatin1(kFormat) ||
        root.value(QStringLiteral("version")).toInt(0) > kVersion) {
        core::log(core::LogLevel::kWarning,
                  "Ignoring unreadable annotation file " + m_file.toStdString());
        return;
    }
    for (const auto& value : root.value(QStringLiteral("files")).toArray()) {
        const QJsonObject object = value.toObject();
        const QString path = object.value(QStringLiteral("path")).toString();
        if (path.isEmpty() || m_entries.size() >= kLimit) {
            continue;
        }
        bool ok = false;
        const std::uint64_t hash =
            object.value(QStringLiteral("hash")).toString().toULongLong(&ok, 16);
        if (!ok) {
            continue;
        }
        m_entries.push_back({.path = path,
                             .key = {.length = static_cast<std::uint64_t>(
                                         object.value(QStringLiteral("length")).toInteger(0)),
                                     .hash = hash},
                             .annotations = annotationsFromJson(object)});
    }
}

AnnotationStore::Result AnnotationStore::load(const QString& path, const FileKey& key)
{
    ensureLoaded();
    const auto it =
        std::ranges::find_if(m_entries, [&path](const Entry& entry) { return entry.path == path; });
    if (it == m_entries.end()) {
        return {};
    }
    if (it->key != key) {
        m_entries.erase(it);
        write();
        return {.lookup = Lookup::kMismatch, .annotations = {}};
    }
    return {.lookup = Lookup::kFound, .annotations = it->annotations};
}

void AnnotationStore::save(const QString& path, const FileKey& key,
                           const FileAnnotations& annotations)
{
    ensureLoaded();
    const auto it =
        std::ranges::find_if(m_entries, [&path](const Entry& entry) { return entry.path == path; });
    if (it != m_entries.end()) {
        if (it->key == key && it->annotations == annotations && it == m_entries.begin()) {
            return;  // unchanged
        }
        m_entries.erase(it);
    } else if (annotations.empty()) {
        return;
    }
    if (!annotations.empty()) {
        m_entries.insert(m_entries.begin(), {.path = path, .key = key, .annotations = annotations});
        if (m_entries.size() > kLimit) {
            m_entries.resize(kLimit);
        }
    }
    write();
}

std::size_t AnnotationStore::size()
{
    ensureLoaded();
    return m_entries.size();
}

void AnnotationStore::write() const
{
    QJsonArray files;
    for (const Entry& entry : m_entries) {
        QJsonObject object = toJson(entry.annotations);
        object.insert(QStringLiteral("path"), entry.path);
        object.insert(QStringLiteral("length"), static_cast<qint64>(entry.key.length));
        object.insert(QStringLiteral("hash"), QString::number(entry.key.hash, 16));
        files.append(object);
    }
    const QJsonObject root{{QStringLiteral("format"), QString::fromLatin1(kFormat)},
                           {QStringLiteral("version"), kVersion},
                           {QStringLiteral("files"), files}};
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0 || !file.commit()) {
        core::log(core::LogLevel::kWarning, "Cannot write annotation file " + m_file.toStdString());
    }
}

}  // namespace lv::model
