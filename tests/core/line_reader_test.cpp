#include "core/index/indexer.h"
#include "core/index/line_index.h"
#include "core/platform/file.h"
#include "core/reader/line_reader.h"
#include "log_writer.h"
#include "loggen/loggen.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using lv::core::Encoding;
using lv::core::File;
using lv::core::LineCount;
using lv::core::LineIndex;
using lv::core::LineNumber;
using lv::core::LineReader;
using lv::core::LineReaderOptions;
using lv::test::TempDir;

namespace
{

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Reference: split UTF-8/single-byte text into lines, dropping "\n" and a "\r" before it.
std::vector<std::string> referenceLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        start = end + 1;
    }
    return lines;
}

std::string asString(std::span<const std::byte> data)
{
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

struct Opened
{
    Opened(File openedFile, Encoding encoding, std::uint64_t dataStart)
        : file(std::move(openedFile))
        , index(encoding, dataStart)
    {}

    File file;
    LineIndex index;
};

std::unique_ptr<Opened> openAndIndex(const std::filesystem::path& path,
                                     Encoding encoding = Encoding::kUtf8,
                                     std::uint64_t dataStart = 0)
{
    auto file = File::open(path);
    REQUIRE(file.has_value());
    auto opened = std::make_unique<Opened>(std::move(*file), encoding, dataStart);
    REQUIRE(lv::core::Indexer::run(opened->file, opened->index, std::stop_token{}).has_value());
    return opened;
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    lv::test::LogWriter writer(path, true);
    writer.append(text);
}

}  // namespace

TEST_CASE("random line reads equal the reference", "[fs][reader]")
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.sizeBytes = std::uint64_t{5} * 1024 * 1024;
    write.out = dir / "gen.log";
    REQUIRE(lv::loggen::writeLog(write).ok());
    const auto expected = referenceLines(readFile(write.out));

    const auto opened = openAndIndex(write.out);
    const LineReader reader(opened->file, opened->index);
    const auto snapshot = opened->index.snapshot();
    REQUIRE(snapshot.lineCount().get() == expected.size());

    std::mt19937_64 random(4);
    for (int round = 0; round < 300; ++round) {
        const std::uint64_t first = random() % expected.size();
        const std::uint64_t count =
            std::min<std::uint64_t>(1 + (random() % 80), expected.size() - first);
        const auto block = reader.readLines(snapshot, LineNumber(first), LineCount(count));
        REQUIRE(block.has_value());
        REQUIRE(block->lines.size() == count);
        for (std::uint64_t i = 0; i < count; ++i) {
            REQUIRE(asString(block->line(i)) == expected[first + i]);
        }
    }
}

TEST_CASE("a screen of consecutive lines needs few reads", "[fs][reader]")
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.sizeBytes = std::uint64_t{2} * 1024 * 1024;
    write.out = dir / "gen.log";
    REQUIRE(lv::loggen::writeLog(write).ok());
    const auto opened = openAndIndex(write.out);
    const LineReader reader(opened->file, opened->index);
    const auto snapshot = opened->index.snapshot();

    const auto block = reader.readLines(snapshot, LineNumber(5000), LineCount(1000));
    REQUIRE(block.has_value());
    CHECK(reader.readCalls() == 1);  // ~130 KB of contiguous lines in one read
    // The same lines again come from the cache.
    REQUIRE(reader.readLines(snapshot, LineNumber(5100), LineCount(50)).has_value());
    CHECK(reader.readCalls() == 1);
}

TEST_CASE("CRLF, empty lines, and the pending tail", "[fs][reader]")
{
    const TempDir dir;
    writeText(dir / "a.log", "one\r\n\r\n\ntwo\rx\nlast\r");
    const auto opened = openAndIndex(dir / "a.log");
    const LineReader reader(opened->file, opened->index);
    const auto snapshot = opened->index.snapshot();
    const auto block = reader.readLines(snapshot, LineNumber(0), snapshot.lineCount());
    REQUIRE(block.has_value());
    std::vector<std::string> lines;
    lines.reserve(block->lines.size());
    for (std::size_t i = 0; i < block->lines.size(); ++i) {
        lines.push_back(asString(block->line(i)));
    }
    // The tail keeps its CR: it may still become "\r\n" once the writer continues.
    CHECK(lines == std::vector<std::string>{"one", "", "", "two\rx", "last\r"});
}

TEST_CASE("UTF-16 lines without terminators", "[fs][reader]")
{
    const TempDir dir;
    writeText(dir / "u.log", std::string("\xFF\xFE"
                                         "a\0\r\0\n\0b\0",
                                         10));
    const auto opened = openAndIndex(dir / "u.log", Encoding::kUtf16Le, 2);
    const LineReader reader(opened->file, opened->index);
    const auto snapshot = opened->index.snapshot();
    const auto block = reader.readLines(snapshot, LineNumber(0), LineCount(2));
    REQUIRE(block.has_value());
    CHECK(asString(block->line(0)) == std::string("a\0", 2));
    CHECK(asString(block->line(1)) == std::string("b\0", 2));
}

TEST_CASE("a 16 MiB line is read, longer lines are cut (F-FILE-7)", "[fs][reader]")
{
    const TempDir dir;
    const std::string longLine(std::size_t{16} * 1024 * 1024, 'x');
    writeText(dir / "l.log", "short\n" + longLine + "\n" + longLine + "yz\nend\n");
    const auto opened = openAndIndex(dir / "l.log");
    const LineReader reader(opened->file, opened->index);
    const auto snapshot = opened->index.snapshot();
    const auto block = reader.readLines(snapshot, LineNumber(0), LineCount(4));
    REQUIRE(block.has_value());
    CHECK(asString(block->line(0)) == "short");
    CHECK(block->line(1).size() == longLine.size());
    CHECK_FALSE(block->lines[1].truncated);
    CHECK(block->line(2).size() == longLine.size());
    CHECK(block->lines[2].truncated);
    CHECK(asString(block->line(3)) == "end");
}

TEST_CASE("small cache settings still give correct results", "[fs][reader]")
{
    const TempDir dir;
    std::string text;
    for (int i = 0; i < 2000; ++i) {
        text +=
            "line " + std::to_string(i) + std::string(static_cast<std::size_t>(i % 50), '.') + "\n";
    }
    writeText(dir / "s.log", text);
    const auto expected = referenceLines(text);
    const auto opened = openAndIndex(dir / "s.log");
    LineReaderOptions options;
    options.pageSize = 64;
    options.cachePages = 3;
    options.maxLineBytes = 30;
    const LineReader reader(opened->file, opened->index, options);
    const auto snapshot = opened->index.snapshot();
    for (std::uint64_t first = 0; first < expected.size(); first += 37) {
        const std::uint64_t count = std::min<std::uint64_t>(23, expected.size() - first);
        const auto block = reader.readLines(snapshot, LineNumber(first), LineCount(count));
        REQUIRE(block.has_value());
        for (std::uint64_t i = 0; i < count; ++i) {
            REQUIRE(asString(block->line(i)) == expected[first + i].substr(0, 30));
            REQUIRE(block->lines[i].truncated == (expected[first + i].size() > 30));
        }
    }
}

TEST_CASE("reading a truncated file returns an error", "[fs][reader]")
{
    const TempDir dir;
    const auto path = dir / "t.log";
    lv::test::LogWriter writer(path, true);
    writer.append("aaaa\nbbbb\ncccc\n");
    const auto opened = openAndIndex(path);
    LineReader reader(opened->file, opened->index);
    const auto snapshot = opened->index.snapshot();
    writer.truncate();
    writer.append("x\n");
    reader.clearCache();
    const auto block = reader.readLines(snapshot, LineNumber(1), LineCount(2));
    REQUIRE_FALSE(block.has_value());
    CHECK(block.error().code == lv::core::ErrorCode::kOutOfRange);
}
