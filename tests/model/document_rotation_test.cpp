#include "core/index/line_index.h"
#include "log_writer.h"
#include "loggen/loggen.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "model/search_pool.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using lv::core::LineNumber;
using lv::model::Document;
using lv::model::FilteredDocument;
using lv::test::LogWriter;
using lv::test::TempDir;

namespace
{

template <typename Condition>
bool waitFor(Condition condition, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

void spin(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

std::string readAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Raw line ranges of `text` as the index would store them.
std::vector<lv::core::ByteRange> expectedLines(const std::string& text)
{
    std::vector<lv::core::ByteRange> out;
    std::uint64_t begin = 0;
    while (begin < text.size()) {
        const auto feed = text.find('\n', begin);
        const std::uint64_t end = feed == std::string::npos ? text.size() : feed;
        out.emplace_back(lv::core::ByteOffset(begin), lv::core::ByteOffset(end));
        begin = feed == std::string::npos ? text.size() : feed + 1;
    }
    return out;
}

std::vector<lv::core::ByteRange> documentLines(const Document& document)
{
    std::vector<lv::core::ByteRange> out;
    const auto content = document.content();
    if (!content) {
        return out;
    }
    const auto snapshot = content->index.snapshot();
    for (std::uint64_t i = 0; i < snapshot.lineCount().get(); ++i) {
        out.push_back(content->index.rawLineRange(snapshot, LineNumber(i)));
    }
    return out;
}

// The document shows exactly the current file.
bool showsFile(const Document& document, const std::filesystem::path& path)
{
    const auto content = document.content();
    if (!content || document.state() != Document::State::kReady) {
        return false;
    }
    const std::string text = readAll(path);
    return content->index.snapshot().indexedEnd.get() == text.size() &&
           documentLines(document) == expectedLines(text);
}

std::uint64_t countContaining(const std::string& text, std::string_view word)
{
    std::uint64_t count = 0;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto feed = text.find('\n', begin);
        const auto end = feed == std::string::npos ? text.size() : feed;
        if (std::string_view(text).substr(begin, end - begin).find(word) !=
            std::string_view::npos) {
            ++count;
        }
        begin = feed == std::string::npos ? text.size() : feed + 1;
    }
    return count;
}

// Runs loggen in live mode on a thread while the event loop keeps running.
lv::loggen::WriteResult runLive(const lv::loggen::WriteOptions& options)
{
    lv::loggen::WriteResult result;
    bool finished = false;
    std::jthread writer([&] {
        result = lv::loggen::writeLog(options);
        finished = true;  // read after join only
    });
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < static_cast<qint64>(options.durationMs) + 200) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    writer.join();
    REQUIRE(finished);
    return result;
}

}  // namespace

TEST_CASE("rotation and truncation end with exactly the current file", "[model][tail][rotation]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    {
        LogWriter writer(path, true);
        writer.append("first line\n");
    }
    Document document(path);
    document.setPollInterval(100);
    FilteredDocument filter(document, lv::model::searchThreadPool());
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    REQUIRE(filter.apply({.text = "ERROR"}) == std::nullopt);
    const QSignalSpy notices(&document, &Document::notice);

    lv::loggen::WriteOptions options;
    options.out = path;
    options.appendRateBytesPerSecond = std::uint64_t{400} * 1024;
    options.durationMs = 1500;
    const bool rotate = GENERATE(true, false);
    CAPTURE(rotate);
    if (rotate) {
        options.rotateEveryBytes = std::uint64_t{128} * 1024;
    } else {
        options.truncateEveryBytes = std::uint64_t{128} * 1024;
    }
    const auto written = runLive(options);
    REQUIRE(written.ok());
    CHECK(written.rotations + written.truncations >= 3);

    REQUIRE(waitFor([&] { return showsFile(document, path); }));
    // The filter was re-run for the current file only.
    const std::string text = readAll(path);
    REQUIRE(waitFor([&] {
        return filter.state() == FilteredDocument::State::kDone &&
               filter.matchCount() == countContaining(text, "ERROR");
    }));
    CHECK(notices.count() >= 1);
    CHECK_FALSE(document.isMissing());
}

TEST_CASE("a deleted file keeps its content until it reappears", "[model][tail][rotation]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    {
        LogWriter writer(path, true);
        writer.append("one\ntwo\nthree\n");
    }
    Document document(path);
    document.setPollInterval(100);
    document.open();
    REQUIRE(waitFor([&] { return showsFile(document, path); }));
    const auto generation = document.generation();
    const QSignalSpy notices(&document, &Document::notice);

    std::filesystem::remove(path);
    REQUIRE(waitFor([&] { return document.isMissing(); }));
    CHECK(notices.count() == 1);
    CHECK(document.state() == Document::State::kReady);
    CHECK(document.lineCount().get() == 3);  // content stays
    CHECK(document.generation() == generation);
    spin(300);
    CHECK(notices.count() == 1);  // reported once

    {
        LogWriter writer(path, true);
        writer.append("new\n");
    }
    REQUIRE(
        waitFor([&] { return document.generation() > generation && showsFile(document, path); }));
    CHECK_FALSE(document.isMissing());
    CHECK(document.lineCount().get() == 1);
    REQUIRE(notices.count() == 2);
    CHECK(notices.at(1).at(0).toString().contains(QStringLiteral("reappeared")));
}

TEST_CASE("rotation away without a new file shows the missing state", "[model][tail][rotation]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    {
        LogWriter writer(path, true);
        writer.append("old\n");
    }
    Document document(path);
    document.setPollInterval(100);
    document.open();
    REQUIRE(waitFor([&] { return showsFile(document, path); }));
    std::filesystem::rename(path, dir / "app.log.1");
    REQUIRE(waitFor([&] { return document.isMissing(); }));
    CHECK(document.lineCount().get() == 1);
    // Reloading while missing reports an error instead of crashing.
    document.reload();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kError; }));
}
