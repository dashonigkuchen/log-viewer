#include "core/util/log.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

namespace
{

// Restores the default (discarding) sink when a test ends.
struct SinkGuard
{
    SinkGuard() = default;
    SinkGuard(const SinkGuard&) = delete;
    SinkGuard& operator=(const SinkGuard&) = delete;
    SinkGuard(SinkGuard&&) = delete;
    SinkGuard& operator=(SinkGuard&&) = delete;

    ~SinkGuard() { lv::core::setLogSink({}); }
};

}  // namespace

TEST_CASE("log forwards messages to the installed sink", "[core][log]")
{
    const SinkGuard guard;
    std::vector<std::pair<lv::core::LogLevel, std::string>> received;
    lv::core::setLogSink([&received](lv::core::LogLevel level, std::string_view message) {
        received.emplace_back(level, std::string(message));
    });

    lv::core::log(lv::core::LogLevel::kWarning, "disk is slow");
    lv::core::log(lv::core::LogLevel::kError, "boom");

    REQUIRE(received.size() == 2);
    CHECK(received[0].first == lv::core::LogLevel::kWarning);
    CHECK(received[0].second == "disk is slow");
    CHECK(received[1].first == lv::core::LogLevel::kError);
}

TEST_CASE("log without a sink discards messages", "[core][log]")
{
    const SinkGuard guard;
    lv::core::setLogSink({});
    lv::core::log(lv::core::LogLevel::kInfo, "nobody listens");
    SUCCEED();
}

TEST_CASE("a sink may install another sink while logging", "[core][log]")
{
    const SinkGuard guard;
    int secondCalls = 0;
    lv::core::setLogSink([&secondCalls](lv::core::LogLevel, std::string_view) {
        lv::core::setLogSink(
            [&secondCalls](lv::core::LogLevel, std::string_view) { ++secondCalls; });
    });
    lv::core::log(lv::core::LogLevel::kInfo, "first");
    lv::core::log(lv::core::LogLevel::kInfo, "second");
    CHECK(secondCalls == 1);
}

TEST_CASE("toString names every level", "[core][log]")
{
    CHECK(lv::core::toString(lv::core::LogLevel::kDebug) == "DEBUG");
    CHECK(lv::core::toString(lv::core::LogLevel::kInfo) == "INFO");
    CHECK(lv::core::toString(lv::core::LogLevel::kWarning) == "WARNING");
    CHECK(lv::core::toString(lv::core::LogLevel::kError) == "ERROR");
}
