#include "core/index/line_index.h"
#include "log_writer.h"
#include "model/document.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <vector>

using lv::core::LineNumber;
using lv::model::Document;
using lv::test::LogWriter;
using lv::test::TempDir;

namespace
{

template <typename Condition>
bool waitFor(Condition condition, int timeoutMs = 10000)
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

struct IndexState
{
    lv::core::LineIndex::Snapshot snapshot;
    std::vector<lv::core::ByteRange> lines;

    bool operator==(const IndexState& other) const
    {
        return snapshot.completeLines == other.snapshot.completeLines &&
               snapshot.indexedEnd == other.snapshot.indexedEnd &&
               snapshot.lastLineEnd == other.snapshot.lastLineEnd && lines == other.lines;
    }
};

IndexState stateOf(const Document& document)
{
    const auto content = document.content();
    REQUIRE(content);
    IndexState state;
    state.snapshot = content->index.snapshot();
    for (std::uint64_t i = 0; i < state.snapshot.lineCount().get(); ++i) {
        state.lines.push_back(content->index.rawLineRange(state.snapshot, LineNumber(i)));
    }
    return state;
}

IndexState freshState(const std::filesystem::path& path)
{
    Document fresh(path);
    fresh.open();
    REQUIRE(waitFor([&] { return fresh.state() == Document::State::kReady; }));
    return stateOf(fresh);
}

bool readyWithSize(const Document& document, std::uint64_t size)
{
    return waitFor([&] {
        const auto content = document.content();
        return document.state() == Document::State::kReady && content &&
               content->index.snapshot().indexedEnd.get() == size;
    });
}

}  // namespace

TEST_CASE("appended data is indexed incrementally like a fresh open", "[model][tail]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    LogWriter writer(path, true);
    writer.append("first\nsecond\n");
    const bool notifications = GENERATE(true, false);
    CAPTURE(notifications);

    Document document(path);
    document.setPollInterval(100);
    document.setWatchNotificationsEnabled(notifications);
    document.open();
    REQUIRE(readyWithSize(document, 13));
    const auto generation = document.generation();

    std::string all = "first\nsecond\n";
    const std::vector<std::string> steps = {
        "third\n",          // complete line
        "partial",          // no trailing newline: a pending tail
        " line continues",  // the partial line grows
        " done\nnext",      // completed, plus a new partial line
        "\n",               // completed
        "a\nb\nc\n",        // several lines at once
    };
    for (const auto& step : steps) {
        CAPTURE(step);
        writer.append(step);
        all += step;
        REQUIRE(readyWithSize(document, all.size()));
        CHECK(stateOf(document) == freshState(path));
    }
    CHECK(document.lineCount().get() == 8);      // first second third "partial…done" next a b c
    CHECK(document.generation() == generation);  // never reloaded
    CHECK(document.tailRuns() >= 1);
}

TEST_CASE("growth during the initial indexing is picked up", "[model][tail]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    LogWriter writer(path, true);
    std::string text;
    for (int i = 0; i < 200000; ++i) {
        text += "line with some content number " + std::to_string(i) + "\n";
    }
    writer.append(text);
    Document document(path);
    document.setPollInterval(100);
    document.open();
    writer.append("late line\n");  // most likely while indexing
    text += "late line\n";
    REQUIRE(readyWithSize(document, text.size()));
    CHECK(document.lineCount().get() == 200001);
}

TEST_CASE("truncation and replacement reload the document", "[model][tail]")
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
    REQUIRE(readyWithSize(document, 14));
    const auto generation = document.generation();
    {
        LogWriter writer(path, true);  // truncates
        writer.append("x\n");
    }
    REQUIRE(waitFor([&] { return document.generation() > generation; }));
    REQUIRE(readyWithSize(document, 2));
    CHECK(document.lineCount().get() == 1);

    const auto beforeReplace = document.generation();
    std::filesystem::rename(path, dir / "app.log.1");
    {
        LogWriter writer(path, true);
        writer.append("new file\nwith two lines\n");
    }
    REQUIRE(waitFor([&] { return document.generation() > beforeReplace; }));
    REQUIRE(readyWithSize(document, 24));
    CHECK(document.lineCount().get() == 2);
}

TEST_CASE("new lines arrive within the polling latency budget (P-TAIL-1)", "[model][tail]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    LogWriter writer(path, true);
    writer.append("start\n");
    Document document(path);
    document.setPollInterval(100);
    document.setWatchNotificationsEnabled(false);  // polling only: budget interval + 100 ms
    document.open();
    REQUIRE(readyWithSize(document, 6));
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 150) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    qint64 worst = 0;
    std::uint64_t size = 6;
    for (int i = 0; i < 5; ++i) {
        QElapsedTimer timer;
        timer.start();
        writer.append("tick\n");
        size += 5;
        REQUIRE(readyWithSize(document, size));
        worst = std::max(worst, timer.elapsed());
    }
    WARN("worst polling latency: " << worst << " ms");
    CHECK(worst <= 100 + 100 + 150);  // interval + 100 ms budget, plus slack for slow CI runners
}
