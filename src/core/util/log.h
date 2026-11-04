#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace lv::core
{

enum class LogLevel : std::uint8_t
{
    kDebug,
    kInfo,
    kWarning,
    kError,
};

// Receives every log message. Installed once by the application.
using LogSink = std::function<void(LogLevel, std::string_view)>;

// Installs the global log sink. An empty sink discards all messages (the default).
// Thread-safety: safe to call from any thread; messages logged concurrently go either to the old
// or to the new sink.
void setLogSink(LogSink sink);

// Forwards a message to the installed sink. Never call this in hot loops.
// Thread-safety: safe to call from any thread. The sink is called outside of internal locks, so a
// sink must be thread-safe itself.
void log(LogLevel level, std::string_view message);

// Returns "DEBUG", "INFO", "WARNING", or "ERROR".
[[nodiscard]] std::string_view toString(LogLevel level) noexcept;

}  // namespace lv::core
