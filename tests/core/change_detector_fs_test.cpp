#include "core/platform/file.h"
#include "core/tail/change_detector.h"
#include "log_writer.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using lv::core::Change;
using lv::core::ChangeDetector;
using lv::core::File;
using lv::core::FileSnapshot;
using lv::test::LogWriter;
using lv::test::TempDir;

namespace
{

FileSnapshot snap(const File& file)
{
    auto snapshot = ChangeDetector::snapshot(file);
    REQUIRE(snapshot.has_value());
    return *snapshot;
}

Change check(const File& file, const FileSnapshot& last)
{
    auto change = ChangeDetector::check(file, file.path(), last);
    REQUIRE(change.has_value());
    return *change;
}

File openFile(const std::filesystem::path& path)
{
    auto file = File::open(path);
    REQUIRE(file.has_value());
    return std::move(*file);
}

}  // namespace

TEST_CASE("change detector on real files: grow and truncate", "[core][fs][tail]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    LogWriter writer(path, true);
    writer.append("line 1\n");
    const File file = openFile(path);
    auto last = snap(file);
    CHECK(last.size == 7);
    CHECK(check(file, last) == Change::kUnchanged);

    writer.append("line 2\n");
    CHECK(check(file, last) == Change::kGrown);
    last = snap(file);
    CHECK(check(file, last) == Change::kUnchanged);

    writer.truncate();
    writer.append("x\n");
    CHECK(check(file, last) == Change::kTruncated);
    last = snap(file);

    // Rewritten from the start without becoming smaller.
    writer.truncate();
    writer.append("y\nmore text\n");
    CHECK(check(file, last) == Change::kTruncated);
}

TEST_CASE("change detector on real files: a grown prefix beyond 4 KiB", "[core][fs][tail]")
{
    const TempDir dir;
    const auto path = dir / "big.log";
    LogWriter writer(path, true);
    writer.append(std::string(5000, 'a'));
    const File file = openFile(path);
    const auto last = snap(file);
    CHECK(last.fingerprint.length == 4096);
    writer.append("tail\n");
    CHECK(check(file, last) == Change::kGrown);
}

TEST_CASE("change detector on real files: replace, delete, recreate", "[core][fs][tail]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    {
        LogWriter writer(path, true);
        writer.append("old\n");
    }
    const File file = openFile(path);
    const auto last = snap(file);

    SECTION("rename and recreate (rotation)")
    {
        std::filesystem::rename(path, dir / "app.log.1");
        CHECK(check(file, last) == Change::kMissing);  // rotated away, not yet recreated
        {
            LogWriter writer(path, true);
            writer.append("new\n");
        }
        CHECK(check(file, last) == Change::kReplaced);
    }

    SECTION("delete and recreate")
    {
        std::filesystem::remove(path);
        CHECK(check(file, last) == Change::kMissing);
        // The open handle still reads the old content.
        CHECK(snap(file) == last);
        {
            LogWriter writer(path, true);
            writer.append("again\n");
        }
        CHECK(check(file, last) == Change::kReplaced);
    }
}
