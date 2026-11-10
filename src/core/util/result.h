#pragma once

#include <cstdint>
#include <expected>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

// Error handling for lv_core (ARCHITECTURE §8): functions that can fail return Result<T>.
// No exceptions cross module boundaries.
namespace lv::core
{

enum class ErrorCode : std::uint8_t
{
    kNotFound,
    kPermissionDenied,
    kIsDirectory,
    kIoError,
    kOutOfRange,
    kInvalidArgument,
    kCanceled,
    kUnsupported,
    kOutOfMemory,
};

[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

struct Error
{
    ErrorCode code = ErrorCode::kIoError;
    // OS error code (errno or GetLastError()), 0 if none.
    std::int64_t systemCode = 0;
    // English, human-readable, without a trailing period.
    std::string message;

    // "IoError: read failed (system error 5)".
    [[nodiscard]] std::string toString() const;

    bool operator==(const Error&) const = default;
};

std::ostream& operator<<(std::ostream& out, const Error& error);

template <typename T>
using Result = std::expected<T, Error>;

// Convenience for `return makeError(ErrorCode::kNotFound, "...");` in functions returning Result.
[[nodiscard]] inline std::unexpected<Error> makeError(ErrorCode code, std::string message,
                                                      std::int64_t systemCode = 0)
{
    return std::unexpected<Error>(
        Error{.code = code, .systemCode = systemCode, .message = std::move(message)});
}

}  // namespace lv::core
