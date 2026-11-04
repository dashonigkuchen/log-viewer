#pragma once

#include <QString>

namespace lv::model
{

// Returns the directory for settings, the application log, and crash dumps (SPEC F-SET-2):
// %APPDATA%\LogViewer on Windows, $XDG_CONFIG_HOME/LogViewer (usually ~/.config/LogViewer)
// elsewhere. The environment variable LV_SETTINGS_DIR overrides the location (used by tests).
// Portable mode is added in roadmap step M2-08. The directory is not created.
[[nodiscard]] QString settingsDirectory();

}  // namespace lv::model
