#include "core/platform/file.h"
#include "log_writer.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using lv::core::ByteOffset;
using lv::core::ErrorCode;
using lv::core::File;
using lv::test::LogWriter;
using lv::test::TempDir;

namespace
{

std::string readString(const File& file, std::uint64_t offset, std::size_t count)
{
    std::vector<std::byte> buffer(count);
    const auto read = file.readAt(ByteOffset(offset), buffer);
    REQUIRE(read.has_value());
    return {reinterpret_cast<const char*>(buffer.data()), *read};
}

void writeFile(const std::filesystem::path& path, std::string_view content)
{
    LogWriter writer(path, true);
    writer.append(content);
}

}  // namespace

TEST_CASE("File reads at arbitrary offsets", "[core][fs]")
{
    const TempDir dir;
    const auto path = dir / "a.log";
    writeFile(path, "0123456789abcdef");

    auto file = File::open(path);
    REQUIRE(file.has_value());
    CHECK(file->path() == path);
    CHECK(file->size().value() == 16);
    CHECK(readString(*file, 0, 4) == "0123");
    CHECK(readString(*file, 10, 6) == "abcdef");
    CHECK(readString(*file, 14, 10) == "ef");   // short read at the end
    CHECK(readString(*file, 16, 4).empty());    // at EOF
    CHECK(readString(*file, 1000, 4).empty());  // past EOF
    CHECK(readString(*file, 3, 0).empty());     // empty buffer
}

TEST_CASE("File reads a file larger than one read chunk", "[core][fs]")
{
    const TempDir dir;
    const auto path = dir / "big.log";
    std::string content;
    for (int i = 0; content.size() < std::size_t{3} * 1024 * 1024; ++i) {
        content += "line " + std::to_string(i) + "\n";
    }
    writeFile(path, content);
    const auto file = File::open(path);
    REQUIRE(file.has_value());
    CHECK(readString(*file, 0, content.size() + 10) == content);
    CHECK(readString(*file, 1234567, 100) == content.substr(1234567, 100));
}

TEST_CASE("File is moveable", "[core][fs]")
{
    const TempDir dir;
    writeFile(dir / "a.log", "abc");
    auto first = File::open(dir / "a.log");
    REQUIRE(first.has_value());
    File moved = std::move(*first);
    CHECK(readString(moved, 0, 3) == "abc");
    writeFile(dir / "b.log", "xyz");
    auto second = File::open(dir / "b.log");
    REQUIRE(second.has_value());
    moved = std::move(*second);
    CHECK(readString(moved, 0, 3) == "xyz");
}

TEST_CASE("A writer can append while the file is open", "[core][fs]")
{
    const TempDir dir;
    const auto path = dir / "live.log";
    LogWriter writer(path, true);
    writer.append("first\n");

    const auto file = File::open(path);
    REQUIRE(file.has_value());
    CHECK(file->size().value() == 6);

    writer.append("second\n");
    CHECK(file->size().value() == 13);
    CHECK(readString(*file, 6, 7) == "second\n");

    SECTION("truncation while open")
    {
        writer.truncate();
        CHECK(file->size().value() == 0);
        CHECK(readString(*file, 0, 10).empty());
        writer.append("new\n");
        CHECK(readString(*file, 0, 10) == "new\n");
    }
}

TEST_CASE("The file can be opened while a logger holds it", "[core][fs]")
{
    const TempDir dir;
    const auto path = dir / "held.log";
    // The writer opens first (Windows: FILE_SHARE_READ only), then the viewer opens it.
    LogWriter writer(path, true);
    writer.append("x\n");
    const auto file = File::open(path);
    REQUIRE(file.has_value());
    CHECK(readString(*file, 0, 2) == "x\n");
}

TEST_CASE("The file can be renamed and deleted while open", "[core][fs]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    writeFile(path, "old content\n");
    const auto file = File::open(path);
    REQUIRE(file.has_value());
    const auto original = file->identity();
    REQUIRE(original.has_value());

    SECTION("rename and recreate (rotation)")
    {
        const auto rotated = dir / "app.log.1";
        std::filesystem::rename(path, rotated);
        writeFile(path, "new content\n");

        CHECK(readString(*file, 0, 11) == "old content");
        const auto atPath = File::identityOfPath(path);
        REQUIRE(atPath.has_value());
        CHECK(*atPath != *original);
        CHECK(File::identityOfPath(rotated).value() == *original);
    }
    SECTION("delete")
    {
        std::error_code ec;
        CHECK(std::filesystem::remove(path, ec));
        CHECK_FALSE(ec);
        CHECK(readString(*file, 0, 11) == "old content");
    }
}

TEST_CASE("identity is stable for the same file", "[core][fs]")
{
    const TempDir dir;
    writeFile(dir / "a.log", "a");
    writeFile(dir / "b.log", "b");
    const auto a1 = File::open(dir / "a.log");
    const auto a2 = File::open(dir / "a.log");
    REQUIRE(a1.has_value());
    REQUIRE(a2.has_value());
    CHECK(a1->identity().value() == a2->identity().value());
    CHECK(File::identityOfPath(dir / "a.log").value() == a1->identity().value());
    CHECK(File::identityOfPath(dir / "b.log").value() != a1->identity().value());
}

TEST_CASE("open reports errors", "[core][fs]")
{
    const TempDir dir;

    const auto missing = File::open(dir / "missing.log");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::kNotFound);
    CHECK(missing.error().message.find("missing.log") != std::string::npos);
    CHECK(missing.error().systemCode != 0);

    const auto missingDir = File::open(dir / "nope" / "x.log");
    REQUIRE_FALSE(missingDir.has_value());
    CHECK(missingDir.error().code == ErrorCode::kNotFound);

    const auto directory = File::open(dir.path());
    REQUIRE_FALSE(directory.has_value());
    CHECK(directory.error().code == ErrorCode::kIsDirectory);

    CHECK(File::identityOfPath(dir / "missing.log").error().code == ErrorCode::kNotFound);

#if !defined(_WIN32)
    if (::geteuid() != 0) {  // root ignores file permissions
        const auto locked = dir / "locked.log";
        writeFile(locked, "secret");
        std::filesystem::permissions(locked, std::filesystem::perms::none);
        const auto denied = File::open(locked);
        REQUIRE_FALSE(denied.has_value());
        CHECK(denied.error().code == ErrorCode::kPermissionDenied);
    }
#endif
}
