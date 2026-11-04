#include "core/platform/file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace lv::core
{

namespace
{

int fd(std::intptr_t handle)
{
    return static_cast<int>(handle);
}

tl::unexpected<Error> systemError(std::string what, const std::filesystem::path& path, int code)
{
    ErrorCode errorCode = ErrorCode::kIoError;
    switch (code) {
    case ENOENT:
    case ENOTDIR:
        errorCode = ErrorCode::kNotFound;
        break;
    case EACCES:
    case EPERM:
        errorCode = ErrorCode::kPermissionDenied;
        break;
    case EISDIR:
        errorCode = ErrorCode::kIsDirectory;
        break;
    case ENOMEM:
        errorCode = ErrorCode::kOutOfMemory;
        break;
    default:
        break;
    }
    what += " '";
    what += path.string();
    what += "': ";
    what += std::system_category().message(code);
    return makeError(errorCode, std::move(what), code);
}

}  // namespace

Result<File> File::open(const std::filesystem::path& path)
{
    int descriptor = -1;
    do {
        descriptor = ::open(path.c_str(),
                            O_RDONLY | O_CLOEXEC);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        return systemError("cannot open", path, errno);
    }
    File file(descriptor, path);
    struct stat info{};
    if (::fstat(descriptor, &info) != 0) {
        return systemError("cannot stat", path, errno);
    }
    if (S_ISDIR(info.st_mode)) {
        return makeError(ErrorCode::kIsDirectory,
                         "cannot open '" + path.string() + "': is a directory");
    }
    return file;
}

void File::close() noexcept
{
    if (m_handle != kInvalidHandle) {
        ::close(fd(m_handle));
        m_handle = kInvalidHandle;
    }
}

Result<std::size_t> File::readAt(ByteOffset offset, std::span<std::byte> buffer) const
{
    std::size_t total = 0;
    while (total < buffer.size()) {
        const std::uint64_t position = offset.get() + total;
        if (position > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
            break;
        }
        const ssize_t count = ::pread(fd(m_handle), buffer.data() + total, buffer.size() - total,
                                      static_cast<off_t>(position));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemError("cannot read", m_path, errno);
        }
        if (count == 0) {
            break;  // end of file
        }
        total += static_cast<std::size_t>(count);
    }
    return total;
}

Result<std::uint64_t> File::size() const
{
    struct stat info{};
    if (::fstat(fd(m_handle), &info) != 0) {
        return systemError("cannot stat", m_path, errno);
    }
    return static_cast<std::uint64_t>(info.st_size);
}

Result<FileIdentity> File::identity() const
{
    struct stat info{};
    if (::fstat(fd(m_handle), &info) != 0) {
        return systemError("cannot stat", m_path, errno);
    }
    FileIdentity identity;
    identity.volume = static_cast<std::uint64_t>(info.st_dev);
    const auto inode = static_cast<std::uint64_t>(info.st_ino);
    std::memcpy(identity.fileId.data(), &inode, sizeof(inode));
    return identity;
}

}  // namespace lv::core
