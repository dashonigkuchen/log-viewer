#include "log_writer.h"
#include "model/annotation_store.h"
#include "model/bookmarks.h"
#include "model/document.h"
#include "model/mark_set.h"
#include "temp_dir.h"

#include <QFile>

#include <catch2/catch_test_macros.hpp>

using lv::core::LineNumber;
using lv::model::AnnotationStore;
using lv::model::FileAnnotations;
using lv::model::FileKey;

namespace
{

QString qpath(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

// Key of an existing file (an unreadable file gives an impossible key, so lookups fail).
FileKey keyOf(const QString& path)
{
    return FileKey::read(path).value_or(FileKey{.length = ~std::uint64_t{0}, .hash = 0});
}

FileAnnotations sample()
{
    return FileAnnotations{.marks = {{.line = 1, .color = 0}, {.line = 5, .color = 3}},
                           .bookmarks = {{.line = 2, .name = QStringLiteral("start of request")}}};
}

}  // namespace

TEST_CASE("annotations are restored for the same file", "[model][persist]")
{
    const lv::test::TempDir dir;
    const QString store = qpath(dir / "annotations.json");
    const QString path = qpath(dir / "a.log");
    {
        lv::test::LogWriter writer(dir / "a.log", true);
        writer.append("a\nb\nc\n");
    }
    REQUIRE(FileKey::read(path).has_value());
    const FileKey key = keyOf(path);
    {
        AnnotationStore annotations(store);
        CHECK(annotations.load(path, key).lookup == AnnotationStore::Lookup::kNone);
        annotations.save(path, key, sample());
    }
    // A new store instance reads the file.
    AnnotationStore annotations(store);
    const auto result = annotations.load(path, key);
    CHECK(result.lookup == AnnotationStore::Lookup::kFound);
    CHECK(result.annotations == sample());

    // Saving empty annotations removes the entry.
    annotations.save(path, key, {});
    CHECK(annotations.size() == 0);
}

TEST_CASE("a changed file discards its annotations", "[model][persist]")
{
    const lv::test::TempDir dir;
    const QString store = qpath(dir / "annotations.json");
    const QString path = qpath(dir / "a.log");
    {
        lv::test::LogWriter writer(dir / "a.log", true);
        writer.append("first version\n");
    }
    AnnotationStore annotations(store);
    annotations.save(path, keyOf(path), sample());
    {
        lv::test::LogWriter writer(dir / "a.log", true);  // rewritten from the start
        writer.append("second version\n");
    }
    const FileKey key = keyOf(path);
    CHECK(annotations.load(path, key).lookup == AnnotationStore::Lookup::kMismatch);
    CHECK(annotations.load(path, key).lookup == AnnotationStore::Lookup::kNone);  // discarded

    // Appending after the first 64 KiB keeps the key: logs that grew still match.
    {
        lv::test::LogWriter writer(dir / "b.log", true);
        writer.append(std::string(std::size_t{70} * 1024, 'x') + "\n");
    }
    const QString grown = qpath(dir / "b.log");
    REQUIRE(FileKey::read(grown).has_value());
    const FileKey before = keyOf(grown);
    {
        lv::test::LogWriter writer(dir / "b.log", false);
        writer.append("more\n");
    }
    CHECK(keyOf(grown) == before);
}

TEST_CASE("the store keeps the 500 most recently saved files", "[model][persist]")
{
    const lv::test::TempDir dir;
    AnnotationStore annotations(qpath(dir / "annotations.json"));
    const FileKey key{.length = 1, .hash = 42};
    for (std::size_t i = 0; i < AnnotationStore::kLimit + 10; ++i) {
        annotations.save(QStringLiteral("/logs/%1.log").arg(i), key, sample());
    }
    CHECK(annotations.size() == AnnotationStore::kLimit);
    CHECK(annotations.load(QStringLiteral("/logs/0.log"), key).lookup ==
          AnnotationStore::Lookup::kNone);
    CHECK(annotations.load(QStringLiteral("/logs/509.log"), key).lookup ==
          AnnotationStore::Lookup::kFound);
    // Saving an old entry again makes it most recent.
    annotations.save(QStringLiteral("/logs/10.log"), key,
                     FileAnnotations{.marks = {{.line = 9, .color = 0}}, .bookmarks = {}});
    annotations.save(QStringLiteral("/logs/new.log"), key, sample());
    CHECK(annotations.load(QStringLiteral("/logs/10.log"), key).lookup ==
          AnnotationStore::Lookup::kFound);
    CHECK(annotations.load(QStringLiteral("/logs/11.log"), key).lookup ==
          AnnotationStore::Lookup::kNone);

    // A corrupt file is ignored.
    const QString broken = qpath(dir / "broken.json");
    QFile file(broken);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("{not json");
    file.close();
    AnnotationStore corrupt(broken);
    CHECK(corrupt.size() == 0);
}

TEST_CASE("annotations convert to and from marks and bookmarks", "[model][persist]")
{
    const lv::test::TempDir dir;
    {
        lv::test::LogWriter writer(dir / "a.log", true);
        writer.append("a\nb\nc\n");
    }
    const lv::model::Document document(dir / "a.log");
    lv::model::MarkSet marks(document);
    lv::model::Bookmarks bookmarks(document);
    // Line 5 is beyond the 3 lines of the file: dropped.
    CHECK(sample().applyTo(marks, bookmarks, 3) == 2);
    CHECK(marks.colorOf(LineNumber(1)) == 0);
    CHECK_FALSE(marks.contains(LineNumber(5)));
    CHECK(bookmarks.name(LineNumber(2)) == QStringLiteral("start of request"));
    const FileAnnotations back = FileAnnotations::of(marks, bookmarks);
    CHECK(back.marks.size() == 1);
    CHECK(back.bookmarks == sample().bookmarks);
}
