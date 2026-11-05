#include "core/reader/line_reader.h"
#include "log_writer.h"
#include "loggen/loggen.h"
#include "model/document.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using lv::model::Document;
using lv::test::TempDir;

namespace
{

// Processes events until `condition` holds or the timeout expires.
template <typename Condition>
bool waitFor(Condition condition, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    lv::test::LogWriter writer(path, true);
    writer.append(text);
}

std::string lineText(const Document& document, std::uint64_t line)
{
    const auto content = document.content();
    REQUIRE(content != nullptr);
    const auto block = content->reader.readLines(
        content->index.snapshot(), lv::core::LineNumber(line), lv::core::LineCount(1));
    REQUIRE(block.has_value());
    const auto bytes = block->line(0);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

}  // namespace

TEST_CASE("opening a generated log reports progress and becomes ready", "[model][document]")
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.sizeBytes = std::uint64_t{20} * 1024 * 1024;
    write.out = dir / "gen.log";
    const auto written = lv::loggen::writeLog(write);
    REQUIRE(written.ok());

    Document document(write.out);
    QSignalSpy progress(&document, &Document::indexingProgress);
    QSignalSpy appended(&document, &Document::linesAppended);
    QSignalSpy states(&document, &Document::stateChanged);
    document.open();
    CHECK(document.state() == Document::State::kIndexing);
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));

    CHECK(document.lineCount().get() == written.lines);
    CHECK(progress.count() >= 1);
    CHECK(progress.last().at(0).toULongLong() == written.bytes);
    CHECK(appended.last().at(0).toULongLong() == written.lines);
    CHECK(states.last().at(0).value<Document::State>() == Document::State::kReady);
    CHECK(document.content()->generation == document.generation());
}

TEST_CASE("open errors produce the error state (F-FILE-12)", "[model][document]")
{
    const TempDir dir;
    SECTION("missing file")
    {
        Document document(dir / "missing.log");
        document.open();
        REQUIRE(waitFor([&] { return document.state() == Document::State::kError; }));
        CHECK_FALSE(document.errorMessage().isEmpty());
        CHECK(document.content() == nullptr);
        CHECK(document.lineCount().get() == 0);
    }
    SECTION("directory")
    {
        Document document(dir.path());
        document.open();
        REQUIRE(waitFor([&] { return document.state() == Document::State::kError; }));
        CHECK(document.errorMessage().contains("director", Qt::CaseInsensitive));
    }
}

TEST_CASE("reload starts a new generation (F-FILE-8)", "[model][document]")
{
    const TempDir dir;
    const auto path = dir / "a.log";
    writeText(path, "one\ntwo\n");
    Document document(path);
    QSignalSpy resets(&document, &Document::reset);
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    const auto first = document.generation();
    CHECK(document.lineCount().get() == 2);

    writeText(path, "one\ntwo\nthree\n");
    document.reload();
    CHECK(document.generation() == first + 1);
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    CHECK(document.lineCount().get() == 3);
    REQUIRE(resets.count() == 2);
    CHECK(resets.last().at(0).toULongLong() == first + 1);
}

TEST_CASE("encoding override re-indexes the file (F-ENC-3)", "[model][document]")
{
    const TempDir dir;
    const auto path = dir / "cp.log";
    writeText(path, "Gr\xC3\xB6\xC3\x9F"
                    "e\n");  // UTF-8 "Größe"
    Document document(path);
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    CHECK(document.content()->index.encoding() == lv::core::Encoding::kUtf8);

    document.setEncodingOverride(lv::core::Encoding::kWindows1252);
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    CHECK(document.content()->index.encoding() == lv::core::Encoding::kWindows1252);
    CHECK(lineText(document, 0) == "Gr\xC3\xB6\xC3\x9F"
                                   "e");

    // A UTF-16 BOM is skipped only when the override matches it.
    writeText(path, std::string("\xFF\xFE"
                                "a\0\n\0",
                                6));
    document.setEncodingOverride(lv::core::Encoding::kUtf16Le);
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    CHECK(lineText(document, 0) == std::string("a\0", 2));
    document.setEncodingOverride(std::nullopt);
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    CHECK(document.content()->index.encoding() == lv::core::Encoding::kUtf16Le);
}

TEST_CASE("closing during indexing joins the worker quickly", "[model][document]")
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.profile = lv::loggen::Profile::kShort;
    write.sizeBytes = std::uint64_t{64} * 1024 * 1024;
    write.out = dir / "big.log";
    REQUIRE(lv::loggen::writeLog(write).ok());

    Document document(write.out);
    document.open();
    QElapsedTimer timer;
    timer.start();
    document.close();
    CHECK(timer.elapsed() < 1000);
    CHECK(document.state() == Document::State::kClosed);
    CHECK(document.content() == nullptr);
    // Events queued by the stopped worker are dropped.
    QCoreApplication::processEvents();
    CHECK(document.state() == Document::State::kClosed);
}

TEST_CASE("destroying a document while indexing is safe", "[model][document]")
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.sizeBytes = std::uint64_t{50} * 1024 * 1024;
    write.out = dir / "big.log";
    REQUIRE(lv::loggen::writeLog(write).ok());
    for (int i = 0; i < 5; ++i) {
        auto document = std::make_unique<Document>(write.out);
        document->open();
        QCoreApplication::processEvents();
        document.reset();
    }
    QCoreApplication::processEvents();
}
