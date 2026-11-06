#pragma once

#include <QString>

namespace lv::model
{

// Returns the directory for settings, the application log, and crash dumps (SPEC F-SET-2):
// %APPDATA%\LogViewer on Windows, $XDG_CONFIG_HOME/LogViewer (usually ~/.config/LogViewer)
// elsewhere. The environment variable LV_SETTINGS_DIR overrides the location (used by tests).
// Portable mode: if `portable.ini` exists in the application directory, that directory is used.
// The directory is not created.
[[nodiscard]] QString settingsDirectory();

// Same, with an explicit application directory (empty = no portable check); for tests.
[[nodiscard]] QString settingsDirectory(const QString& applicationDirectory);

// True if `applicationDirectory` contains portable.ini (SPEC F-SET-2).
[[nodiscard]] bool isPortable(const QString& applicationDirectory);

}  // namespace lv::model
