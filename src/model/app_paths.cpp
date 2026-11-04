#include "model/app_paths.h"

#include <QDir>
#include <QStandardPaths>
#include <QtGlobal>

namespace lv::model
{

QString settingsDirectory()
{
    const QString override = qEnvironmentVariable("LV_SETTINGS_DIR");
    if (!override.isEmpty()) {
        return QDir::cleanPath(override);
    }
    // %APPDATA% (roaming profile) is only set on Windows.
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString base =
        appData.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                          : appData;
    return QDir::cleanPath(base + QStringLiteral("/LogViewer"));
}

}  // namespace lv::model
