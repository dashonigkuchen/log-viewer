#include "log_writer.h"
#include "model/app_paths.h"
#include "model/settings.h"
#include "temp_dir.h"

#include <QDir>
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

TEST_CASE("dialog settings round-trip and clamp", "[model][settings]")
{
    const lv::test::TempDir dir;
    {
        Settings settings(qpath(dir.path()));
        CHECK(settings.pollInterval() == Settings::kDefaultPollMs);
        CHECK(settings.maxColumns() == Settings::kDefaultMaxColumns);
        CHECK(settings.filterHistorySize() == Settings::kDefaultHistorySize);
        CHECK(settings.restoreSession());
        CHECK(settings.singleInstance());
        CHECK(settings.searchThreads() == 0);
        CHECK_FALSE(settings.currentLineColor());
        const QSignalSpy other(&settings, &Settings::settingsChanged);
        const QSignalSpy view(&settings, &Settings::viewSettingsChanged);
        settings.setPollInterval(50);  // clamped
        settings.setMaxColumns(5000);
        settings.setFilterHistorySize(20);
        settings.setRestoreSession(false);
        settings.setSingleInstance(false);
        settings.setSearchThreads(3);
        settings.setCurrentLineColor(0xFF112233U);
        settings.setMarkColor(0x80FF0000U);
        settings.setMatchColor(0x8000FF00U);
        CHECK(settings.pollInterval() == Settings::kMinPollMs);
        CHECK(other.count() == 6);
        CHECK(view.count() == 4);  // max columns + 3 colors
    }
    const Settings loaded(qpath(dir.path()));
    CHECK(loaded.pollInterval() == Settings::kMinPollMs);
    CHECK(loaded.maxColumns() == 5000);
    CHECK(loaded.filterHistorySize() == 20);
    CHECK_FALSE(loaded.restoreSession());
    CHECK_FALSE(loaded.singleInstance());
    CHECK(loaded.searchThreads() == 3);
    CHECK(loaded.currentLineColor() == 0xFF112233U);
    CHECK(loaded.markColor() == 0x80FF0000U);
    CHECK(loaded.matchColor() == 0x8000FF00U);
    CHECK(loaded.colorScheme() == lv::model::ColorScheme::kSystem);
}

TEST_CASE("version 1 settings are migrated", "[model][settings]")
{
    const lv::test::TempDir dir;
    {
        QSettings raw(qpath(dir / "logviewer.ini"), QSettings::IniFormat);
        raw.setValue(QStringLiteral("General/schemaVersion"), 1);
        raw.setValue(QStringLiteral("View/pollInterval"), 2500);
        raw.setValue(QStringLiteral("View/tabWidth"), 4);
    }
    const Settings settings(qpath(dir.path()));
    CHECK(settings.pollInterval() == 2500);
    CHECK(settings.tabWidth() == 4);
    const QSettings raw(settings.filePath(), QSettings::IniFormat);
    CHECK(raw.value(QStringLiteral("General/schemaVersion")).toInt() == Settings::kSchemaVersion);
    CHECK_FALSE(raw.contains(QStringLiteral("View/pollInterval")));
}

TEST_CASE("a corrupt settings file is backed up and replaced", "[model][settings]")
{
    const lv::test::TempDir dir;
    {
        QFile file(qpath(dir / "logviewer.ini"));
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[View\ntabWidth=4\n");  // unterminated section: QSettings::FormatError
    }
    Settings settings(qpath(dir.path()));
    // Compare normalized paths: Qt uses "/" while std::filesystem uses "\\" on Windows.
    CHECK(QDir::cleanPath(settings.corruptBackup()) ==
          QDir::cleanPath(qpath(dir / "logviewer.ini.corrupt")));
    CHECK(QFile::exists(settings.corruptBackup()));
    CHECK(settings.tabWidth() == Settings::kDefaultTabWidth);
    settings.setTabWidth(2);
    settings.sync();
    const Settings again(qpath(dir.path()));
    CHECK(again.corruptBackup().isEmpty());
    CHECK(again.tabWidth() == 2);
}

TEST_CASE("color scheme and theme defaults", "[model][settings]")
{
    const lv::test::TempDir dir;
    {
        // A version 2 file with the old fixed defaults: migrated to "theme default".
        QSettings raw(qpath(dir / "logviewer.ini"), QSettings::IniFormat);
        raw.setValue(QStringLiteral("General/schemaVersion"), 2);
        raw.setValue(QStringLiteral("Colors/mark"), QStringLiteral("#5A78AAFF"));
        raw.setValue(QStringLiteral("Colors/match"), QStringLiteral("#80112233"));  // user's own
    }
    {
        Settings settings(qpath(dir.path()));
        CHECK_FALSE(settings.markColor().has_value());
        CHECK(settings.matchColor() == 0x80112233U);
        const QSignalSpy changed(&settings, &Settings::viewSettingsChanged);
        settings.setColorScheme(lv::model::ColorScheme::kDark);
        settings.setColorScheme(lv::model::ColorScheme::kDark);
        settings.setMatchColor(std::nullopt);
        CHECK(changed.count() == 2);
    }
    const Settings loaded(qpath(dir.path()));
    CHECK(loaded.colorScheme() == lv::model::ColorScheme::kDark);
    CHECK_FALSE(loaded.matchColor().has_value());
}
