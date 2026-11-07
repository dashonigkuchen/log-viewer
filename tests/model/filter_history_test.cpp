#include "model/filter_history.h"
#include "temp_dir.h"

#include <QSettings>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using lv::core::Query;
using lv::core::QueryMode;
using lv::model::FilterHistory;

TEST_CASE("filter history order, dedup, and limit", "[model][history]")
{
    FilterHistory history(nullptr, 3);
    const QSignalSpy changed(&history, &FilterHistory::changed);
    history.add({.text = "a"});
    history.add({.text = "b", .mode = QueryMode::kRegex});
    history.add({.text = "a"});  // moves to the top
    REQUIRE(history.entries().size() == 2);
    CHECK(history.entries()[0].text == "a");
    CHECK(history.entries()[1].text == "b");
    // Different options are different entries; whole word is ignored outside literal mode.
    history.add({.text = "a", .caseSensitive = true});
    history.add({.text = "b", .mode = QueryMode::kRegex, .wholeWord = true});
    REQUIRE(history.entries().size() == 3);
    CHECK(history.entries()[0].text == "b");
    CHECK_FALSE(history.entries()[0].wholeWord);
    history.add({.text = "c"});
    history.add({.text = ""});             // ignored
    CHECK(history.entries().size() == 3);  // limit
    CHECK(history.entries()[0].text == "c");
    history.remove(0);
    CHECK(history.entries()[0].text == "b");
    history.clear();
    CHECK(history.entries().empty());
    CHECK(changed.count() == 8);
}

TEST_CASE("filter history round-trips through settings", "[model][history]")
{
    const lv::test::TempDir dir;
    const QString ini = QString::fromStdString((dir / "s.ini").string());
    {
        QSettings settings(ini, QSettings::IniFormat);
        FilterHistory history;
        history.add({.text = "x", .wholeWord = true});
        history.add({.text = R"(/E/ AND NOT "h")", .mode = QueryMode::kBoolean, .invert = true});
        history.add({.text = "größe", .mode = QueryMode::kRegex, .caseSensitive = true});
        history.save(settings);
    }
    QSettings settings(ini, QSettings::IniFormat);
    FilterHistory loaded;
    loaded.load(settings);
    REQUIRE(loaded.entries().size() == 3);
    CHECK(loaded.entries()[0] ==
          Query{.text = "größe", .mode = QueryMode::kRegex, .caseSensitive = true});
    CHECK(loaded.entries()[1].invert);
    CHECK(loaded.entries()[2].wholeWord);
}
