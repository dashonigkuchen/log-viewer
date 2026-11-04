#include "core/util/result.h"

namespace lv::core
{

std::string_view toString(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::kNotFound:
        return "NotFound";
    case ErrorCode::kPermissionDenied:
        return "PermissionDenied";
    case ErrorCode::kIsDirectory:
        return "IsDirectory";
    case ErrorCode::kIoError:
        return "IoError";
    case ErrorCode::kOutOfRange:
        return "OutOfRange";
    case ErrorCode::kInvalidArgument:
        return "InvalidArgument";
    case ErrorCode::kCanceled:
        return "Canceled";
    case ErrorCode::kUnsupported:
        return "Unsupported";
    case ErrorCode::kOutOfMemory:
        return "OutOfMemory";
    }
    return "Unknown";
}

std::string Error::toString() const
{
    std::string text(lv::core::toString(code));
    if (!message.empty()) {
        text += ": ";
        text += message;
    }
    if (systemCode != 0) {
        text += " (system error " + std::to_string(systemCode) + ")";
    }
    return text;
}

std::ostream& operator<<(std::ostream& out, const Error& error)
{
    return out << error.toString();
}

}  // namespace lv::core
