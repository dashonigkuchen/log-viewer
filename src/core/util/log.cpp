#include "core/util/log.h"

#include <memory>
#include <mutex>
#include <utility>

namespace lv::core
{

namespace
{

struct SinkState
{
    std::mutex mutex;
    std::shared_ptr<const LogSink> sink;
};

SinkState& sinkState()
{
    static SinkState state;
    return state;
}

}  // namespace

void setLogSink(LogSink sink)
{
    auto shared = sink ? std::make_shared<const LogSink>(std::move(sink)) : nullptr;
    auto& state = sinkState();
    const std::scoped_lock lock(state.mutex);
    state.sink = std::move(shared);
}

void log(LogLevel level, std::string_view message)
{
    std::shared_ptr<const LogSink> sink;
    {
        auto& state = sinkState();
        const std::scoped_lock lock(state.mutex);
        sink = state.sink;
    }
    if (sink) {
        (*sink)(level, message);
    }
}

std::string_view toString(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::kDebug:
        return "DEBUG";
    case LogLevel::kInfo:
        return "INFO";
    case LogLevel::kWarning:
        return "WARNING";
    case LogLevel::kError:
        return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace lv::core
