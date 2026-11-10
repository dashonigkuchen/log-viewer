#include "model/session.h"
#include "temp_dir.h"

#include <QSettings>

#include <catch2/catch_test_macros.hpp>

using lv::model::Session;

TEST_CASE("sessions round-trip through the settings", "[model][session]")
{
    const lv::test::TempDir dir;
    const QString path = QString::fromStdU16String((dir / "s.ini").u16string());
    Session session;
    session.tabs.push_back({.path = QStringLiteral("/logs/a.log"),
                            .topRow = 120,
                            .currentRow = 130,
                            .filter = lv::core::Query{.text = "ERROR",
                                                      .mode = lv::core::QueryMode::kRegex,
                                                      .caseSensitive = true,
                                                      .wholeWord = false,
                                                      .invert = true},
                            .following = false});
    session.tabs.push_back({.path = QStringLiteral("/logs/b.log"),
                            .topRow = 0,
                            .currentRow = std::nullopt,
                            .filter = std::nullopt,
                            .following = true});
    session.currentTab = 1;
    session.geometry = QByteArray("geo");
    session.windowState = QByteArray("state");
    {
        QSettings settings(path, QSettings::IniFormat);
        session.save(settings);
    }
    QSettings settings(path, QSettings::IniFormat);
    CHECK(Session::load(settings) == session);

    // Saving a shorter session drops the old tabs.
    Session small;
    small.tabs.push_back({.path = QStringLiteral("/c.log"),
                          .topRow = 0,
                          .currentRow = std::nullopt,
                          .filter = std::nullopt,
                          .following = false});
    small.currentTab = 5;  // out of range on load: clamped
    small.save(settings);
    const Session loaded = Session::load(settings);
    CHECK(loaded.tabs.size() == 1);
    CHECK(loaded.currentTab == 0);
}
