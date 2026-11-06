#include "log_writer.h"
#include "model/recent_files.h"
#include "temp_dir.h"

#include <QSettings>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using lv::model::RecentFiles;

namespace
{

QString path(const lv::test::TempDir& dir, const char* name)
{
    return RecentFiles::normalize(QString::fromStdString((dir / name).string()));
}

void touch(const lv::test::TempDir& dir, const char* name)
{
    lv::test::LogWriter writer(dir / name, true);
    writer.append("x\n");
}

}  // namespace

TEST_CASE("recent files are ordered newest first and deduplicated", "[model][recent]")
{
    const lv::test::TempDir dir;
    RecentFiles recent;
    const QSignalSpy changed(&recent, &RecentFiles::changed);
    recent.add(path(dir, "a.log"));
    recent.add(path(dir, "b.log"));
    recent.add(path(dir, "c.log"));
    recent.add(path(dir, "a.log"));
    CHECK(recent.paths() ==
          QStringList{path(dir, "a.log"), path(dir, "c.log"), path(dir, "b.log")});
    // Another spelling of the same path is the same entry.
    recent.add(QString::fromStdString((dir.path() / "." / "b.log").string()));
    CHECK(recent.paths().size() == 3);
    CHECK(recent.paths().front() == path(dir, "b.log"));
    CHECK(changed.count() == 5);
}

TEST_CASE("recent files keep at most the limit", "[model][recent]")
{
    const lv::test::TempDir dir;
    RecentFiles recent(nullptr, 20);
    for (int i = 0; i < 25; ++i) {
        recent.add(QString::fromStdString((dir / ("f" + std::to_string(i) + ".log")).string()));
    }
    REQUIRE(recent.paths().size() == 20);
    CHECK(recent.paths().front().endsWith(QStringLiteral("f24.log")));
    CHECK(recent.paths().back().endsWith(QStringLiteral("f5.log")));
}

TEST_CASE("missing recent files can be removed", "[model][recent]")
{
    const lv::test::TempDir dir;
    touch(dir, "exists.log");
    RecentFiles recent;
    recent.add(path(dir, "gone.log"));
    recent.add(path(dir, "exists.log"));
    recent.removeMissing();
    CHECK(recent.paths() == QStringList{path(dir, "exists.log")});
    recent.remove(path(dir, "exists.log"));
    CHECK(recent.paths().isEmpty());
}

TEST_CASE("recent files round-trip through settings", "[model][recent]")
{
    const lv::test::TempDir dir;
    const QString ini = QString::fromStdString((dir / "settings.ini").string());
    {
        QSettings settings(ini, QSettings::IniFormat);
        RecentFiles recent;
        recent.add(path(dir, "one.log"));
        recent.add(path(dir, "two.log"));
        recent.save(settings);
    }
    QSettings settings(ini, QSettings::IniFormat);
    RecentFiles loaded(nullptr, 1);
    loaded.load(settings);
    CHECK(loaded.paths() == QStringList{path(dir, "two.log")});  // limit applied on load
}
