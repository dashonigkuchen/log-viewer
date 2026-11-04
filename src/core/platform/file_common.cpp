#include "core/platform/file.h"

#include <utility>

namespace lv::core
{

File::File(NativeHandle handle, std::filesystem::path path) noexcept
    : m_handle(handle)
    , m_path(std::move(path))
{}

File::File(File&& other) noexcept
    : m_handle(std::exchange(other.m_handle, kInvalidHandle))
    , m_path(std::move(other.m_path))
{}

File& File::operator=(File&& other) noexcept
{
    if (this != &other) {
        close();
        m_handle = std::exchange(other.m_handle, kInvalidHandle);
        m_path = std::move(other.m_path);
    }
    return *this;
}

File::~File()
{
    close();
}

Result<FileIdentity> File::identityOfPath(const std::filesystem::path& path)
{
    return open(path).and_then([](const File& file) { return file.identity(); });
}

}  // namespace lv::core
