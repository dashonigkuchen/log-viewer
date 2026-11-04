#include "log_writer.h"

#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

// Test support code may use platform #ifdefs; product code keeps them in src/core/platform/.
namespace lv::test
{

#if defined(_WIN32)

namespace
{

HANDLE native(std::intptr_t handle)
{
    return reinterpret_cast<HANDLE>(handle);  // NOLINT(performance-no-int-to-ptr)
}

}  // namespace

LogWriter::LogWriter(const std::filesystem::path& path, bool truncate)
{
    const HANDLE handle =
        ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                      truncate ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("LogWriter: cannot open " + path.string());
    }
    m_handle = reinterpret_cast<std::intptr_t>(handle);
}

// NOLINTNEXTLINE(readability-make-member-function-const): changes the file, so not const
void LogWriter::append(std::string_view text)
{
    LARGE_INTEGER zero{};
    ::SetFilePointerEx(native(m_handle), zero, nullptr, FILE_END);
    DWORD written = 0;
    if (::WriteFile(native(m_handle), text.data(), static_cast<DWORD>(text.size()), &written,
                    nullptr) == 0 ||
        written != text.size()) {
        throw std::runtime_error("LogWriter: write failed");
    }
    ::FlushFileBuffers(native(m_handle));
}

// NOLINTNEXTLINE(readability-make-member-function-const): changes the file, so not const
void LogWriter::truncate()
{
    LARGE_INTEGER zero{};
    ::SetFilePointerEx(native(m_handle), zero, nullptr, FILE_BEGIN);
    if (::SetEndOfFile(native(m_handle)) == 0) {
        throw std::runtime_error("LogWriter: truncate failed");
    }
}

void LogWriter::close()
{
    if (m_handle != -1) {
        ::CloseHandle(native(m_handle));
        m_handle = -1;
    }
}

#else

LogWriter::LogWriter(const std::filesystem::path& path, bool truncate)
{
    const int flags = O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | (truncate ? O_TRUNC : 0);
    const int descriptor =
        ::open(path.c_str(), flags, 0644);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (descriptor < 0) {
        throw std::runtime_error("LogWriter: cannot open " + path.string());
    }
    m_handle = descriptor;
}

// NOLINTNEXTLINE(readability-make-member-function-const): changes the file, so not const
void LogWriter::append(std::string_view text)
{
    if (::write(static_cast<int>(m_handle), text.data(), text.size()) !=
        static_cast<ssize_t>(text.size())) {
        throw std::runtime_error("LogWriter: write failed");
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const): changes the file, so not const
void LogWriter::truncate()
{
    if (::ftruncate(static_cast<int>(m_handle), 0) != 0) {
        throw std::runtime_error("LogWriter: truncate failed");
    }
}

void LogWriter::close()
{
    if (m_handle != -1) {
        ::close(static_cast<int>(m_handle));
        m_handle = -1;
    }
}

#endif

LogWriter::~LogWriter()
{
    close();
}

}  // namespace lv::test
