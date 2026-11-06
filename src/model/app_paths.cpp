#include "model/app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

namespace lv::model
{

bool isPortable(const QString& applicationDirectory)
{
    return !applicationDirectory.isEmpty() &&
           QFileInfo::exists(QDir(applicationDirectory).filePath(QStringLiteral("portable.ini")));
}

QString settingsDirectory()
{
    // Before a QCoreApplication exists there is no application directory.
    return settingsDirectory(QCoreApplication::instance() != nullptr
                                 ? QCoreApplication::applicationDirPath()
                                 : QString());
}

QString settingsDirectory(const QString& applicationDirectory)
{
    const QString override = qEnvironmentVariable("LV_SETTINGS_DIR");
    if (!override.isEmpty()) {
        return QDir::cleanPath(override);
    }
    if (isPortable(applicationDirectory)) {
        return QDir::cleanPath(applicationDirectory);
    }
    // %APPDATA% (roaming profile) is only set on Windows.
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString base =
        appData.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                          : appData;
    return QDir::cleanPath(base + QStringLiteral("/LogViewer"));
}

}  // namespace lv::model
