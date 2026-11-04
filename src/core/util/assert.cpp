#include "core/util/assert.h"

#include "core/util/log.h"

#include <atomic>
#include <cstdlib>
#include <string>

namespace lv::core
{

namespace
{

std::atomic<AssertHandler>& handlerSlot()
{
    static std::atomic<AssertHandler> slot{nullptr};
    return slot;
}

}  // namespace

AssertHandler setAssertHandler(AssertHandler handler) noexcept
{
    return handlerSlot().exchange(handler);
}

namespace detail
{

void assertionFailed(std::string_view expression, const std::source_location& location)
{
    if (const AssertHandler handler = handlerSlot().load(); handler != nullptr) {
        handler(expression, location);
    }
    std::string message = "Assertion failed: ";
    message += expression;
    message += " at ";
    message += location.file_name();
    message += ':';
    message += std::to_string(location.line());
    log(LogLevel::kError, message);
    std::abort();
}

}  // namespace detail

}  // namespace lv::core
