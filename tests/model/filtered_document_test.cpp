#include "core/concurrency/thread_pool.h"
#include "log_writer.h"
#include "loggen/loggen.h"
#include "model/document.h"
#include "model/document_line_source.h"
#include "model/filtered_document.h"
#include "model/filtered_line_source.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using lv::core::LineCount;
using lv::core::LineNumber;
using lv::core::Query;
using lv::core::QueryMode;
using lv::model::Document;
using lv::model::DocumentLineSource;
using lv::model::FilteredDocument;
using lv::model::FilteredLineSource;
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
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    lv::test::LogWriter writer(path, true);
    writer.append(text);
}

std::vector<std::uint64_t> allMatches(const FilteredDocument& filter)
{
    std::vector<std::uint64_t> out;
    out.reserve(static_cast<std::size_t>(filter.matchCount()));
    for (std::uint64_t i = 0; i < filter.matchCount(); ++i) {
        out.push_back(filter.matchAt(i).get());
    }
    return out;
}

bool waitDone(const FilteredDocument& filter)
{
    return waitFor([&] {
        return filter.state() == FilteredDocument::State::kDone ||
               filter.state() == FilteredDocument::State::kError;
    });
}

}  // namespace

TEST_CASE("filter results and line source", "[model][filter]")
{
    const TempDir dir;
    writeText(dir / "a.log", "INFO start\nERROR one\nINFO x\nERROR two\nERROR three\nend");
    lv::core::ThreadPool pool(2);
    Document document(dir / "a.log");
    const DocumentLineSource lines(document);
    FilteredDocument filter(document, pool);
    const FilteredLineSource source(filter, lines);
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));

    const QSignalSpy appended(&source, &lv::model::LineSource::linesAppended);
    REQUIRE_FALSE(filter.apply(Query{.text = "error"}).has_value());
    REQUIRE(waitDone(filter));
    CHECK(filter.state() == FilteredDocument::State::kDone);
    CHECK(allMatches(filter) == std::vector<std::uint64_t>{1, 3, 4});
    CHECK(source.lineCount().get() == 3);
    CHECK(source.originalLineNumber(LineNumber(2)).get() == 4);
    const auto shown = source.lines(LineNumber(0), LineCount(3));
    REQUIRE(shown.size() == 3);
    CHECK(shown[0].text == QStringLiteral("ERROR one"));
    CHECK(shown[1].text == QStringLiteral("ERROR two"));
    CHECK(shown[2].text == QStringLiteral("ERROR three"));
    CHECK(source.plainLines(LineNumber(1), LineCount(2))[1].text == QStringLiteral("ERROR three"));
    CHECK(filter.lowerBound(LineNumber(2)) == 1);
    CHECK(filter.lowerBound(LineNumber(5)) == 3);
    CHECK(appended.count() >= 1);
    CHECK(filter.progress().searchedLines.get() == 6);

    const auto reader = source.rowReader();
    REQUIRE(reader.has_value());
    if (reader) {
        CHECK(reader->lineForRow(LineNumber(1)).get() == 3);
    }

    filter.clear();
    CHECK(filter.state() == FilteredDocument::State::kInactive);
    CHECK(source.lineCount().get() == 0);
}

TEST_CASE("invalid queries keep the previous filter", "[model][filter]")
{
    const TempDir dir;
    writeText(dir / "a.log", "a\nb\na\n");
    lv::core::ThreadPool pool(1);
    Document document(dir / "a.log");
    FilteredDocument filter(document, pool);
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    REQUIRE_FALSE(filter.apply(Query{.text = "a"}).has_value());
    REQUIRE(waitDone(filter));
    const auto error = filter.apply(Query{.text = "(", .mode = QueryMode::kRegex});
    REQUIRE(error.has_value());
    CHECK(filter.query().value_or(Query{}).text == "a");
    CHECK(allMatches(filter) == std::vector<std::uint64_t>{0, 2});
}

TEST_CASE("a new filter while searching never shows old results", "[model][filter]")
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.sizeBytes = std::uint64_t{20} * 1024 * 1024;
    write.out = dir / "gen.log";
    REQUIRE(lv::loggen::writeLog(write).ok());
    lv::core::ThreadPool pool(2);
    Document document(write.out);
    const DocumentLineSource lines(document);
    FilteredDocument filter(document, pool);
    const FilteredLineSource source(filter, lines);
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));

    // Every listener check: all announced matches must contain the current filter's text.
    bool stale = false;
    QObject::connect(&filter, &FilteredDocument::matchesAppended, [&] {
        const QString needle = QString::fromStdString(filter.query().value_or(Query{}).text);
        const auto count = source.lineCount().get();
        const auto first = count > 50 ? count - 50 : 0;
        for (const auto& line : source.plainLines(LineNumber(first), LineCount(count - first))) {
            if (!line.text.contains(needle, Qt::CaseInsensitive)) {
                stale = true;
            }
        }
    });
    for (const char* text : {"DEBUG", "WARN", "INFO", "ERROR"}) {
        REQUIRE_FALSE(filter.apply(Query{.text = text}).has_value());
        CHECK(filter.matchCount() == 0);  // a new generation starts empty
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    REQUIRE(waitDone(filter));
    CHECK_FALSE(stale);
    CHECK(filter.query().value_or(Query{}).text == "ERROR");
    for (const auto& line : source.plainLines(LineNumber(0), source.lineCount())) {
        REQUIRE(line.text.contains(QStringLiteral("error"), Qt::CaseInsensitive));
    }
    CHECK(filter.matchCount() > 0);
}

TEST_CASE("document reload clears and re-runs the filter", "[model][filter]")
{
    const TempDir dir;
    writeText(dir / "a.log", "x\nhit 1\ny\n");
    lv::core::ThreadPool pool(2);
    Document document(dir / "a.log");
    FilteredDocument filter(document, pool);
    const QSignalSpy resets(&filter, &FilteredDocument::reset);
    document.open();
    // Applied while indexing: waits and runs afterwards.
    REQUIRE_FALSE(filter.apply(Query{.text = "hit"}).has_value());
    REQUIRE(waitDone(filter));
    CHECK(allMatches(filter) == std::vector<std::uint64_t>{1});

    writeText(dir / "a.log", "hit a\nhit b\nz\nhit c\n");
    const auto before = resets.count();
    document.reload();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    REQUIRE(waitDone(filter));
    CHECK(resets.count() > before);
    CHECK(allMatches(filter) == std::vector<std::uint64_t>{0, 1, 3});
}

TEST_CASE("destroying a filter during a search joins cleanly", "[model][filter]")
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.sizeBytes = std::uint64_t{20} * 1024 * 1024;
    write.out = dir / "gen.log";
    REQUIRE(lv::loggen::writeLog(write).ok());
    lv::core::ThreadPool pool(2);
    Document document(write.out);
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    for (int i = 0; i < 5; ++i) {
        FilteredDocument filter(document, pool);
        REQUIRE_FALSE(filter.apply(Query{.text = "e", .mode = QueryMode::kRegex}).has_value());
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    }
    // Pending wake-ups of destroyed filters must not crash.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    SUCCEED();
}
