#pragma once

#include <QString>
#include <QStringList>

namespace lv::model
{

// Local crash diagnostics (SPEC F-ROB-5). On Windows an unhandled exception writes a minidump
// to `<settings directory>/dumps/` (dbghelp, a system library); nothing is uploaded. Elsewhere
// this does nothing (core dumps are the OS's business).

// Maximum number of dump files kept; older ones are removed when the handler is installed.
inline constexpr int kMaxCrashDumps = 5;

// Installs the handler for dumps in `dumpDirectory` (created on demand when a dump is written).
// Returns false if crash dumps are not supported on this platform.
bool installCrashHandler(const QString& dumpDirectory);

// Removes the oldest *.dmp files so at most `keep` remain; returns the removed file names.
// Platform independent (tested on Linux too).
QStringList pruneCrashDumps(const QString& dumpDirectory, int keep);

// Crashes the process on purpose (hidden `--crash-test` command line flag, used by CI to check
// that a dump is written). Does not return.
[[noreturn]] void crashForTest();

}  // namespace lv::model
