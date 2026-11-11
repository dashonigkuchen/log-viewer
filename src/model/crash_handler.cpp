#include "model/crash_handler.h"

#include <QDir>
#include <QFileInfo>

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
// dbghelp.h needs windows.h first.
#include <dbghelp.h>

#include <array>
#include <cwchar>
#endif

namespace lv::model
{

QStringList pruneCrashDumps(const QString& dumpDirectory, int keep)
{
    const QDir dir(dumpDirectory);
    // Newest first; ties broken by name so the order is stable.
    const QFileInfoList dumps =
        dir.entryInfoList({QStringLiteral("*.dmp")}, QDir::Files, QDir::Time | QDir::Name);
    QStringList removed;
    for (qsizetype i = std::max(0, keep); i < dumps.size(); ++i) {
        if (QFile::remove(dumps.at(i).absoluteFilePath())) {
            removed << dumps.at(i).fileName();
        }
    }
    return removed;
}

#ifdef _WIN32

namespace
{

// Written once at install time; read in the exception filter, which must not allocate.
std::array<wchar_t, MAX_PATH> gDumpDirectory{};

LONG WINAPI writeDump(EXCEPTION_POINTERS* exception)
{
    ::CreateDirectoryW(gDumpDirectory.data(), nullptr);  // may already exist
    SYSTEMTIME time{};
    ::GetLocalTime(&time);
    std::array<wchar_t, MAX_PATH> path{};
    std::swprintf(path.data(), path.size(), L"%ls\\logviewer-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
                  gDumpDirectory.data(), time.wYear, time.wMonth, time.wDay, time.wHour,
                  time.wMinute, time.wSecond, ::GetCurrentProcessId());
    const HANDLE file = ::CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = ::GetCurrentThreadId();
        info.ExceptionPointers = exception;
        info.ClientPointers = FALSE;
        // Small dump: stacks, modules, and the thread data; no heap (keeps logs private).
        ::MiniDumpWriteDump(
            ::GetCurrentProcess(), ::GetCurrentProcessId(), file,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
            exception != nullptr ? &info : nullptr, nullptr, nullptr);
        ::CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;  // terminate without the Windows error dialog
}

}  // namespace

bool installCrashHandler(const QString& dumpDirectory)
{
    const QString native = QDir::toNativeSeparators(QDir::cleanPath(dumpDirectory));
    if (native.isEmpty() || native.size() >= static_cast<qsizetype>(gDumpDirectory.size()) - 64) {
        return false;  // leave room for the file name
    }
    native.toWCharArray(gDumpDirectory.data());
    gDumpDirectory.at(static_cast<std::size_t>(native.size())) = L'\0';
    (void)pruneCrashDumps(dumpDirectory, kMaxCrashDumps - 1);  // room for the next one
    ::SetUnhandledExceptionFilter(writeDump);
    return true;
}

void crashForTest()
{
    // A real access violation goes through the unhandled exception filter like a crash would.
    ::RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    std::abort();
}

#else

bool installCrashHandler(const QString& /*dumpDirectory*/)
{
    return false;
}

void crashForTest()
{
    std::abort();
}

#endif

}  // namespace lv::model
