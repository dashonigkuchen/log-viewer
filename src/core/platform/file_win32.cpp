#include "core/platform/file.h"

#include <windows.h>

#include <cstring>
#include <limits>
#include <string>
#include <system_error>

namespace lv::core
{

namespace
{

HANDLE nativeHandle(std::intptr_t handle)
{
    return reinterpret_cast<HANDLE>(handle);  // NOLINT(performance-no-int-to-ptr)
}

tl::unexpected<Error> systemError(std::string what, const std::filesystem::path& path, DWORD code)
{
    ErrorCode errorCode = ErrorCode::kIoError;
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
        errorCode = ErrorCode::kNotFound;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        errorCode = ErrorCode::kPermissionDenied;
        break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        errorCode = ErrorCode::kOutOfMemory;
        break;
    default:
        break;
    }
    what += " '";
    what += path.string();
    what += "': ";
    what += std::system_category().message(static_cast<int>(code));
    return makeError(errorCode, std::move(what), static_cast<std::int64_t>(code));
}

}  // namespace

Result<File> File::open(const std::filesystem::path& path)
{
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return makeError(ErrorCode::kIsDirectory,
                         "cannot open '" + path.string() + "': is a directory");
    }
    const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return systemError("cannot open", path, ::GetLastError());
    }
    return File(reinterpret_cast<NativeHandle>(handle), path);
}

void File::close() noexcept
{
    if (m_handle != kInvalidHandle) {
        ::CloseHandle(nativeHandle(m_handle));
        m_handle = kInvalidHandle;
    }
}

Result<std::size_t> File::readAt(ByteOffset offset, std::span<std::byte> buffer) const
{
    std::size_t total = 0;
    while (total < buffer.size()) {
        const std::uint64_t position = offset.get() + total;
        const std::size_t remaining = buffer.size() - total;
        const auto chunk = static_cast<DWORD>(remaining < std::numeric_limits<DWORD>::max()
                                                  ? remaining
                                                  : std::numeric_limits<DWORD>::max());
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(position & 0xFFFFFFFFULL);
        overlapped.OffsetHigh = static_cast<DWORD>(position >> 32U);
        DWORD count = 0;
        if (::ReadFile(nativeHandle(m_handle), buffer.data() + total, chunk, &count, &overlapped) ==
            0) {
            const DWORD error = ::GetLastError();
            if (error == ERROR_HANDLE_EOF) {
                break;
            }
            return systemError("cannot read", m_path, error);
        }
        if (count == 0) {
            break;
        }
        total += count;
    }
    return total;
}

Result<std::uint64_t> File::size() const
{
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(nativeHandle(m_handle), &size) == 0) {
        return systemError("cannot query size of", m_path, ::GetLastError());
    }
    return static_cast<std::uint64_t>(size.QuadPart);
}

Result<FileIdentity> File::identity() const
{
    FILE_ID_INFO info{};
    if (::GetFileInformationByHandleEx(nativeHandle(m_handle), FileIdInfo, &info, sizeof(info)) ==
        0) {
        return systemError("cannot query file ID of", m_path, ::GetLastError());
    }
    FileIdentity identity;
    identity.volume = info.VolumeSerialNumber;
    static_assert(sizeof(info.FileId.Identifier) == sizeof(identity.fileId));
    std::memcpy(identity.fileId.data(), info.FileId.Identifier, sizeof(identity.fileId));
    return identity;
}

}  // namespace lv::core
