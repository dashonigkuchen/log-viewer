#include "log_writer.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "model/search_pool.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

using lv::core::Query;
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

std::vector<std::uint64_t> allMatches(const FilteredDocument& filter)
{
    std::vector<std::uint64_t> out;
    out.reserve(static_cast<std::size_t>(filter.matchCount()));
    for (std::uint64_t i = 0; i < filter.matchCount(); ++i) {
        out.push_back(filter.matchAt(i).get());
    }
    return out;
}

// Lines of `text` (the last one may be unterminated) for which `matches` is true.
template <typename Predicate>
std::vector<std::uint64_t> expected(std::string_view text, Predicate matches)
{
    std::vector<std::uint64_t> out;
    std::uint64_t line = 0;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t feed = text.find('\n', begin);
        const std::size_t end = feed == std::string_view::npos ? text.size() : feed;
        if (matches(text.substr(begin, end - begin))) {
            out.push_back(line);
        }
        ++line;
        begin = feed == std::string_view::npos ? text.size() : feed + 1;
    }
    return out;
}

bool containsError(std::string_view line)
{
    return line.find("ERROR") != std::string_view::npos ||
           line.find("error") != std::string_view::npos;
}

bool lacksError(std::string_view line)
{
    return !containsError(line);
}

struct Fixture
{
    TempDir dir;
    std::filesystem::path path = dir / "app.log";
    LogWriter writer{path, true};
    std::string text;
    Document document{path};
    FilteredDocument filter{document, lv::model::searchThreadPool()};

    void append(const std::string& data)
    {
        writer.append(data);
        text += data;
    }

    void open()
    {
        document.setPollInterval(100);
        document.open();
    }

    // Waits until the document indexed everything and the filter shows `want`.
    [[nodiscard]] bool settles(const std::vector<std::uint64_t>& want) const
    {
        return waitFor([&] {
            const auto content = document.content();
            return content && document.state() == Document::State::kReady &&
                   content->index.snapshot().indexedEnd.get() == text.size() &&
                   filter.state() == FilteredDocument::State::kDone && allMatches(filter) == want;
        });
    }
};

}  // namespace

TEST_CASE("incremental filtering equals a full search of the final file", "[model][filter][tail]")
{
    Fixture f;
    f.append("INFO start\nERROR one\n");
    f.open();
    REQUIRE(f.filter.apply(Query{.text = "error"}) == std::nullopt);
    REQUIRE(f.settles(expected(f.text, containsError)));
    const auto generation = f.filter.generation();

    const std::vector<std::string> steps = {
        "INFO two\n",
        "ERROR three\n",
        "ERROR partial",        // a matching partial line
        " still partial",       // it grows and still matches
        " done\nINFO",          // completed, plus a new partial line
        " not an e-r-r-o-r\n",  // completed without a match
        "x\nERROR a\ny\nerror b\n",
    };
    for (const auto& step : steps) {
        CAPTURE(step);
        f.append(step);
        REQUIRE(f.settles(expected(f.text, containsError)));
    }
    CHECK(f.filter.generation() == generation);  // incremental, never restarted
    CHECK(f.filter.progress().totalLines.get() == f.document.lineCount().get());
}

TEST_CASE("appends during a running full search are neither lost nor duplicated",
          "[model][filter][tail]")
{
    Fixture f;
    std::string big;
    for (int i = 0; i < 300000; ++i) {
        big += (i % 1000 == 0) ? "ERROR periodic\n" : "INFO filler line with some text\n";
    }
    f.append(big);
    f.open();
    REQUIRE(waitFor([&] { return f.document.state() == Document::State::kReady; }));
    REQUIRE(f.filter.apply(Query{.text = "error"}) == std::nullopt);
    for (int i = 0; i < 20; ++i) {
        f.append(i % 2 == 0 ? "ERROR appended\n" : "INFO appended\n");  // during the search
    }
    REQUIRE(f.settles(expected(f.text, containsError)));
    CHECK(f.filter.matchCount() == 300 + 10);
}

TEST_CASE("a listed partial line that stops matching restarts the filter", "[model][filter][tail]")
{
    Fixture f;
    f.append("ERROR a\nabc");  // the partial line "abc" matches the inverted filter
    f.open();
    REQUIRE(f.filter.apply(Query{.text = "error", .invert = true}) == std::nullopt);
    REQUIRE(f.settles(expected(f.text, lacksError)));
    REQUIRE(allMatches(f.filter) == std::vector<std::uint64_t>{1});
    const auto generation = f.filter.generation();
    f.append(" ERROR\nok\n");  // the listed line now contains ERROR
    REQUIRE(f.settles(expected(f.text, lacksError)));
    CHECK(allMatches(f.filter) == std::vector<std::uint64_t>{2});
    CHECK(f.filter.generation() > generation);
}
