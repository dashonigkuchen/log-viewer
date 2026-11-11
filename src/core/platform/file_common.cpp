#include "core/platform/file.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace lv::core
{
namespace
{

// The hook is rare (tests only), so reads check a flag before taking the lock.
std::atomic<bool> gHookInstalled{false};
std::mutex gHookMutex;
std::shared_ptr<const File::ReadFaultHook> gHook;  // guarded by gHookMutex

}  // namespace

void File::setReadFaultHook(ReadFaultHook hook)
{
    const std::scoped_lock lock(gHookMutex);
    gHook = hook ? std::make_shared<const ReadFaultHook>(std::move(hook)) : nullptr;
    gHookInstalled.store(gHook != nullptr, std::memory_order_release);
}

std::optional<Error> File::injectedFault(ByteOffset offset, std::size_t size) const
{
    if (!gHookInstalled.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    std::shared_ptr<const ReadFaultHook> hook;
    {
        const std::scoped_lock lock(gHookMutex);
        hook = gHook;
    }
    return hook ? (*hook)(m_path, offset, size) : std::nullopt;
}

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
