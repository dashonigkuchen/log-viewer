#include "core/index/indexer.h"
#include "core/index/line_index.h"
#include "core/platform/file.h"
#include "log_writer.h"
#include "loggen/loggen.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stop_token>
#include <string>
#include <vector>

using lv::core::Encoding;
using lv::core::File;
using lv::core::Indexer;
using lv::core::IndexerOptions;
using lv::core::LineIndex;
using lv::core::LineNumber;
using lv::test::TempDir;

namespace
{

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Reference: line end offsets by a byte-wise scan (code-unit aware for UTF-16).
std::vector<std::uint64_t> referenceEnds(const std::string& data, Encoding encoding,
                                         std::uint64_t start)
{
    std::vector<std::uint64_t> ends;
    const bool utf16 = encoding == Encoding::kUtf16Le || encoding == Encoding::kUtf16Be;
    const bool be = encoding == Encoding::kUtf16Be;
    for (std::uint64_t i = start; i < data.size();) {
        if (utf16) {
            if (i + 1 < data.size() && data[i + (be ? 1 : 0)] == '\n' &&
                data[i + (be ? 0 : 1)] == '\0') {
                ends.push_back(i + 2);
            }
            i += 2;
        } else {
            if (data[i] == '\n') {
                ends.push_back(i + 1);
            }
            ++i;
        }
    }
    return ends;
}

std::vector<std::uint64_t> indexedEnds(const LineIndex& index)
{
    const auto snapshot = index.snapshot();
    std::vector<std::uint64_t> ends;
    const std::uint64_t lineFeed = lv::core::traits(index.encoding()).codeUnitSize;
    ends.reserve(static_cast<std::size_t>(snapshot.completeLines.get()));
    for (std::uint64_t i = 0; i < snapshot.completeLines.get(); ++i) {
        ends.push_back(index.rawLineRange(snapshot, LineNumber(i)).end().get() + lineFeed);
    }
    return ends;
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    lv::test::LogWriter writer(path, true);
    writer.append(text);
}

IndexerOptions smallBlocks(std::size_t blockSize)
{
    IndexerOptions options;
    options.blockSize = blockSize;
    options.bufferCount = 2;
    return options;
}

}  // namespace

TEST_CASE("Indexer matches a reference scan on generated logs", "[fs][index]")
{
    const auto profile = GENERATE(lv::loggen::Profile::kSmall, lv::loggen::Profile::kShort,
                                  lv::loggen::Profile::kUtf16, lv::loggen::Profile::kCp1252);
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.profile = profile;
    write.sizeBytes = std::uint64_t{20} * 1024 * 1024;
    write.out = dir / "gen.log";
    REQUIRE(lv::loggen::writeLog(write).ok());
    const std::string data = readFile(write.out);

    const auto sample = std::as_bytes(std::span<const char>(
        data.data(), std::min<std::size_t>(data.size(), lv::core::kDetectionSampleSize)));
    const auto detected = lv::core::detectEncoding(sample);
    LineIndex index(detected.encoding, detected.bomLength);
    const auto file = File::open(write.out);
    REQUIRE(file.has_value());
    const auto stats = Indexer::run(*file, index, std::stop_token{});
    REQUIRE(stats.has_value());
    CHECK(stats->bytesRead == data.size() - detected.bomLength);
    CHECK(indexedEnds(index) == referenceEnds(data, detected.encoding, detected.bomLength));
    CHECK(index.snapshot().indexedEnd.get() == data.size());
}

TEST_CASE("Indexer handles edge-case files", "[fs][index]")
{
    const TempDir dir;
    const std::vector<std::string> inputs{
        "", "a", "\n", "a\r\nb", "\r\r\r", std::string(5000, 'x') + "\n" + "tail"};
    const auto blockSize = GENERATE(std::size_t{1}, std::size_t{3}, std::size_t{4096});
    for (const auto& text : inputs) {
        writeText(dir / "e.log", text);
        const auto file = File::open(dir / "e.log");
        REQUIRE(file.has_value());
        LineIndex index(Encoding::kUtf8, 0);
        REQUIRE(
            Indexer::run(*file, index, std::stop_token{}, {}, smallBlocks(blockSize)).has_value());
        CHECK(indexedEnds(index) == referenceEnds(text, Encoding::kUtf8, 0));
        CHECK(index.snapshot().indexedEnd.get() == text.size());
    }
}

TEST_CASE("incremental indexing equals a full index", "[fs][index]")
{
    const TempDir dir;
    const auto path = dir / "live.log";
    lv::test::LogWriter writer(path, true);
    writer.append("one\ntw");
    const auto file = File::open(path);
    REQUIRE(file.has_value());
    LineIndex incremental(Encoding::kUtf8, 0);
    REQUIRE(Indexer::run(*file, incremental, std::stop_token{}).has_value());
    CHECK(incremental.snapshot().lineCount().get() == 2);

    std::string all = "one\ntw";
    for (int i = 0; i < 50; ++i) {
        const std::string chunk = "o\nline " + std::to_string(i) + "\npartial";
        writer.append(chunk);
        all += chunk;
        const auto stats = Indexer::run(*file, incremental, std::stop_token{}, {}, smallBlocks(7));
        REQUIRE(stats.has_value());
    }
    LineIndex full(Encoding::kUtf8, 0);
    REQUIRE(Indexer::run(*file, full, std::stop_token{}).has_value());
    CHECK(indexedEnds(incremental) == indexedEnds(full));
    CHECK(incremental.snapshot().lineCount() == full.snapshot().lineCount());
    CHECK(indexedEnds(full) == referenceEnds(all, Encoding::kUtf8, 0));
}

TEST_CASE("cancellation leaves a consistent prefix", "[fs][index]")
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.sizeBytes = std::uint64_t{8} * 1024 * 1024;
    write.out = dir / "big.log";
    REQUIRE(lv::loggen::writeLog(write).ok());
    const std::string data = readFile(write.out);
    const auto file = File::open(write.out);
    REQUIRE(file.has_value());

    std::stop_source source;
    LineIndex index(Encoding::kUtf8, 0);
    int callbacks = 0;
    IndexerOptions options = smallBlocks(std::size_t{64} * 1024);
    options.progressInterval = std::chrono::milliseconds(0);
    const auto result = Indexer::run(
        *file, index, source.get_token(),
        [&](const lv::core::IndexProgress& progress) {
            ++callbacks;
            if (progress.indexedBytes >= std::uint64_t{1024} * 1024) {
                source.request_stop();
            }
        },
        options);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == lv::core::ErrorCode::kCanceled);
    const auto snapshot = index.snapshot();
    CHECK(snapshot.indexedEnd.get() < data.size());
    CHECK(snapshot.indexedEnd.get() <= (std::uint64_t{1024} * 1024) + (std::uint64_t{64} * 1024));
    const auto expected =
        referenceEnds(data.substr(0, snapshot.indexedEnd.get()), Encoding::kUtf8, 0);
    CHECK(indexedEnds(index) == expected);
    CHECK(callbacks > 0);

    // Resuming finishes the index.
    REQUIRE(Indexer::run(*file, index, std::stop_token{}).has_value());
    CHECK(indexedEnds(index) == referenceEnds(data, Encoding::kUtf8, 0));
}

TEST_CASE("a truncated file is reported", "[fs][index]")
{
    const TempDir dir;
    const auto path = dir / "t.log";
    lv::test::LogWriter writer(path, true);
    writer.append("aaaa\nbbbb\n");
    const auto file = File::open(path);
    REQUIRE(file.has_value());
    LineIndex index(Encoding::kUtf8, 0);
    REQUIRE(Indexer::run(*file, index, std::stop_token{}).has_value());
    writer.truncate();
    writer.append("x\n");
    const auto result = Indexer::run(*file, index, std::stop_token{});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == lv::core::ErrorCode::kOutOfRange);
    CHECK(index.snapshot().completeLines.get() == 2);  // old prefix unchanged
}

TEST_CASE("progress reports reach the end", "[fs][index]")
{
    const TempDir dir;
    writeText(dir / "p.log", std::string(100000, 'a') + "\n");
    const auto file = File::open(dir / "p.log");
    REQUIRE(file.has_value());
    LineIndex index(Encoding::kUtf8, 0);
    lv::core::IndexProgress last;
    REQUIRE(Indexer::run(
                *file, index, std::stop_token{},
                [&](const lv::core::IndexProgress& p) { last = p; }, smallBlocks(1000))
                .has_value());
    CHECK(last.indexedBytes == 100001);
    CHECK(last.totalBytes == 100001);
    CHECK(last.lines.get() == 1);
}
