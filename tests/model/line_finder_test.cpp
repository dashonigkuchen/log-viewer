#include "core/index/sorted_u64_sequence.h"
#include "core/search/query.h"
#include "log_writer.h"
#include "model/document.h"
#include "model/line_finder.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

#include <string>

using lv::model::Document;
using lv::model::FindDirection;
using lv::model::findLine;

namespace
{

bool waitReady(const Document& document)
{
    QElapsedTimer timer;
    timer.start();
    while (document.state() != Document::State::kReady) {
        if (timer.elapsed() > 20000) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

std::shared_ptr<const lv::core::CompiledQuery> compile(const std::string& text)
{
    auto compiled = lv::core::CompiledQuery::compile({.text = text});
    REQUIRE(compiled.has_value());
    return *compiled;
}

}  // namespace

TEST_CASE("findLine searches forward and backward with wrap-around", "[model][find]")
{
    const lv::test::TempDir dir;
    {
        lv::test::LogWriter writer(dir / "a.log", true);
        writer.append("x\nhit 1\nx\nx\nhit 4\nx\n");
    }
    Document document(dir / "a.log");
    document.open();
    REQUIRE(waitReady(document));
    const auto content = document.content();
    const auto query = compile("hit");
    const std::stop_token none;

    const auto first =
        findLine(*content, *query, nullptr, std::nullopt, FindDirection::kForward, none);
    CHECK(first.line == 1U);
    CHECK_FALSE(first.wrapped);
    CHECK(findLine(*content, *query, nullptr, 1, FindDirection::kForward, none).line == 4U);
    const auto wrapped = findLine(*content, *query, nullptr, 4, FindDirection::kForward, none);
    CHECK(wrapped.line == 1U);
    CHECK(wrapped.wrapped);
    CHECK(findLine(*content, *query, nullptr, 3, FindDirection::kBackward, none).line == 1U);
    const auto back = findLine(*content, *query, nullptr, 1, FindDirection::kBackward, none);
    CHECK(back.line == 4U);
    CHECK(back.wrapped);
    CHECK(findLine(*content, *query, nullptr, std::nullopt, FindDirection::kBackward, none).line ==
          4U);
    // The only match from itself wraps to itself.
    const auto only =
        findLine(*content, *compile("hit 4"), nullptr, 4, FindDirection::kForward, none);
    CHECK(only.line == 4U);
    CHECK(only.wrapped);
    CHECK_FALSE(
        findLine(*content, *compile("nope"), nullptr, 0, FindDirection::kForward, none).line);

    // Restricted to the lines of a filter.
    lv::core::SortedU64Sequence restrict;
    restrict.append(4);
    restrict.publish();
    CHECK(
        findLine(*content, *query, & restrict, std::nullopt, FindDirection::kForward, none).line ==
        4U);
    CHECK(findLine(*content, *query, & restrict, 4, FindDirection::kBackward, none).line == 4U);
}

TEST_CASE("LineFinder delivers results on the UI thread", "[model][find]")
{
    const lv::test::TempDir dir;
    {
        lv::test::LogWriter writer(dir / "a.log", true);
        std::string text;
        for (int i = 0; i < 200000; ++i) {
            text += i == 150000 ? "needle\n" : "hay hay hay hay\n";
        }
        writer.append(text);
    }
    Document document(dir / "a.log");
    document.open();
    REQUIRE(waitReady(document));
    lv::model::LineFinder finder;
    QSignalSpy finished(&finder, &lv::model::LineFinder::finished);
    finder.start(document.content(), compile("needle"), nullptr, 10, FindDirection::kForward);
    CHECK(finder.isRunning());
    REQUIRE(finished.wait(20000));
    const auto result = finished.at(0).at(0).value<lv::model::FindResult>();
    CHECK(result.line == 150000U);
    CHECK_FALSE(finder.isRunning());
    // A canceled search reports nothing.
    finder.start(document.content(), compile("needle"), nullptr, 10, FindDirection::kForward);
    finder.cancel();
    QCoreApplication::processEvents();
    CHECK(finished.size() == 1);
}
