#include "log_writer.h"
#include "model/app_paths.h"
#include "model/settings.h"
#include "temp_dir.h"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using lv::model::Settings;

namespace
{

QString qpath(const std::filesystem::path& path)
{
    return QString::fromStdString(path.string());
}

}  // namespace

TEST_CASE("settings have defaults and round-trip", "[model][settings]")
{
    const lv::test::TempDir dir;
    {
        Settings settings(qpath(dir.path()));
        CHECK(settings.fontFamily().isEmpty());
        CHECK(settings.fontSize() == Settings::kDefaultFontSize);
        CHECK(settings.zoom() == 0);
        CHECK(settings.tabWidth() == 8);
        CHECK(settings.lineNumbersVisible());
        const QSignalSpy changed(&settings, &Settings::viewSettingsChanged);
        settings.setFont(QStringLiteral("DejaVu Sans Mono"), 12);
        settings.setZoom(3);
        settings.setTabWidth(4);
        settings.setLineNumbersVisible(false);
        settings.setTabWidth(4);  // unchanged: no signal
        CHECK(changed.count() == 4);
        CHECK(settings.effectiveFontSize() == 15);
        settings.sync();
        CHECK(QFile::exists(settings.filePath()));
    }
    const Settings loaded(qpath(dir.path()));
    CHECK(loaded.fontFamily() == QStringLiteral("DejaVu Sans Mono"));
    CHECK(loaded.fontSize() == 12);
    CHECK(loaded.zoom() == 3);
    CHECK(loaded.tabWidth() == 4);
    CHECK_FALSE(loaded.lineNumbersVisible());
    const QSettings raw(loaded.filePath(), QSettings::IniFormat);
    CHECK(raw.value(QStringLiteral("General/schemaVersion")).toInt() == Settings::kSchemaVersion);
}

TEST_CASE("settings clamp zoom and reject invalid values", "[model][settings]")
{
    const lv::test::TempDir dir;
    {
        QSettings raw(qpath(dir / "logviewer.ini"), QSettings::IniFormat);
        raw.setValue(QStringLiteral("View/fontSize"), 500);
        raw.setValue(QStringLiteral("View/tabWidth"), QStringLiteral("abc"));
        raw.setValue(QStringLiteral("Unknown/key"), 1);
    }
    Settings settings(qpath(dir.path()));
    CHECK(settings.fontSize() == Settings::kDefaultFontSize);
    CHECK(settings.tabWidth() == Settings::kDefaultTabWidth);
    settings.setZoom(1000);
    CHECK(settings.effectiveFontSize() == Settings::kMaxFontSize);
    settings.setZoom(-1000);
    CHECK(settings.effectiveFontSize() == Settings::kMinFontSize);
}

TEST_CASE("portable mode uses the application directory", "[model][settings]")
{
    const lv::test::TempDir dir;
    const QString appDir = qpath(dir.path());
    qunsetenv("LV_SETTINGS_DIR");
    CHECK_FALSE(lv::model::isPortable(appDir));
    CHECK(lv::model::settingsDirectory(appDir) != QDir::cleanPath(appDir));
    {
        lv::test::LogWriter marker(dir / "portable.ini", true);
        marker.append("");
    }
    CHECK(lv::model::isPortable(appDir));
    CHECK(lv::model::settingsDirectory(appDir) == QDir::cleanPath(appDir));
    CHECK_FALSE(lv::model::isPortable(QString()));
}
