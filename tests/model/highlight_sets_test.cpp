#include "model/highlight_sets.h"
#include "temp_dir.h"

#include <QByteArray>
#include <QSettings>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using lv::model::HighlightRule;
using lv::model::HighlightScope;
using lv::model::HighlightSet;
using lv::model::HighlightSets;
using lv::model::HighlightSpan;

namespace
{

HighlightRule lineRule(const char* pattern, lv::model::Argb background, bool regex = false)
{
    return HighlightRule{.pattern = QString::fromUtf8(pattern),
                         .regex = regex,
                         .caseSensitive = false,
                         .scope = HighlightScope::kWholeLine,
                         .foreground = std::nullopt,
                         .background = background,
                         .enabled = true};
}

HighlightRule matchRule(const char* pattern, lv::model::Argb foreground, bool regex = false)
{
    return HighlightRule{.pattern = QString::fromUtf8(pattern),
                         .regex = regex,
                         .caseSensitive = true,
                         .scope = HighlightScope::kMatchOnly,
                         .foreground = foreground,
                         .background = std::nullopt,
                         .enabled = true};
}

}  // namespace

TEST_CASE("the first whole-line rule wins, match rules are layered", "[model][highlight]")
{
    const HighlightSet set{.name = QStringLiteral("s"),
                           .rules = {lineRule("error", 0xFF0000FFU), lineRule("disk", 0xFF00FF00U),
                                     matchRule("disk", 0xFF111111U),
                                     matchRule("d[a-z]+", 0xFF222222U, true)}};
    const lv::model::CompiledHighlightSet compiled(set);
    REQUIRE(compiled.errors().empty());
    lv::core::MatchScratch scratch;

    // Both whole-line rules match: the first one wins. Case-insensitive.
    const auto both = compiled.evaluate(QStringLiteral("ERROR disk full"), scratch);
    CHECK(both.background == 0xFF0000FFU);
    CHECK_FALSE(both.foreground);
    // Match rules in rule order (the later one is painted on top).
    REQUIRE(both.spans.size() == 2);
    CHECK(both.spans[0] ==
          HighlightSpan{.range = {6, 10}, .foreground = 0xFF111111U, .background = std::nullopt});
    CHECK(both.spans[1] ==
          HighlightSpan{.range = {6, 10}, .foreground = 0xFF222222U, .background = std::nullopt});

    const auto second = compiled.evaluate(QStringLiteral("disk ok"), scratch);
    CHECK(second.background == 0xFF00FF00U);

    const auto none = compiled.evaluate(QStringLiteral("all fine"), scratch);
    CHECK(none.empty());
    // Case-sensitive match rule: "DISK" does not match "disk".
    const auto upper = compiled.evaluate(QStringLiteral("DISK"), scratch);
    CHECK(upper.background == 0xFF00FF00U);
    CHECK(upper.spans.empty());
}

TEST_CASE("disabled and invalid rules are skipped", "[model][highlight]")
{
    HighlightRule disabled = lineRule("a", 0xFF000001U);
    disabled.enabled = false;
    const HighlightSet set{
        .name = QStringLiteral("s"),
        .rules = {disabled, lineRule("(", 0xFF000002U, true), lineRule("a", 0xFF000003U)}};
    const lv::model::CompiledHighlightSet compiled(set);
    REQUIRE(compiled.errors().size() == 1);
    CHECK(compiled.errors().front().rule == 1);
    lv::core::MatchScratch scratch;
    CHECK(compiled.evaluate(QStringLiteral("a"), scratch).background == 0xFF000003U);
}

TEST_CASE("sets keep unique names, a default, and cached compiled forms", "[model][highlight]")
{
    HighlightSets sets;
    REQUIRE(sets.size() == 1);  // built-in default set
    CHECK(sets.set(0).name == QStringLiteral("Default"));
    const auto first = sets.compiled(0);
    CHECK(sets.compiled(0) == first);  // cached

    const std::size_t b = sets.add({.name = QStringLiteral("Default"), .rules = {}});
    CHECK(sets.set(b).name == QStringLiteral("Default (2)"));
    const std::size_t c = sets.add({.name = QStringLiteral("Java"), .rules = {}});

    const QSignalSpy changed(&sets, &HighlightSets::setChanged);
    HighlightSet edited = sets.set(0);
    edited.rules.push_back(lineRule("x", 0xFF123456U));
    sets.replace(0, edited);
    CHECK(changed.count() == 1);
    CHECK(sets.compiled(0) != first);  // recompiled
    sets.replace(0, edited);           // unchanged: no signal
    CHECK(changed.count() == 1);

    sets.setDefaultIndex(c);
    CHECK(sets.remove(b));
    CHECK(sets.defaultIndex() == 1);  // still "Java"
    CHECK(sets.set(sets.defaultIndex()).name == QStringLiteral("Java"));
    CHECK(sets.remove(1));  // the default: falls back to the set before it
    CHECK(sets.defaultIndex() == 0);
    CHECK_FALSE(sets.remove(0));  // the last set stays
}

TEST_CASE("highlight sets round-trip through JSON and settings", "[model][highlight]")
{
    HighlightRule rule = matchRule("id=\\d+", 0x80FFAA00U, true);
    rule.background = 0xFF000000U;
    rule.enabled = false;
    const std::vector<HighlightSet> sets{
        {.name = QStringLiteral("One"), .rules = {lineRule("ERROR", 0xFFFF0000U), rule}},
        {.name = QStringLiteral("Empty"), .rules = {}}};
    const QByteArray json = HighlightSets::toJson(sets);
    CHECK(json.contains("\"#FFFF0000\""));
    const auto back = HighlightSets::fromJson(json);
    REQUIRE(back.has_value());
    CHECK(*back == sets);

    const lv::test::TempDir dir;
    const QString path = QString::fromStdString((dir / "settings.ini").string());
    {
        QSettings settings(path, QSettings::IniFormat);
        HighlightSets model;
        model.add(sets[0]);
        model.setDefaultIndex(1);
        model.save(settings);
    }
    QSettings settings(path, QSettings::IniFormat);
    HighlightSets loaded;
    loaded.load(settings);
    REQUIRE(loaded.size() == 2);
    CHECK(loaded.set(1) == sets[0]);
    CHECK(loaded.defaultIndex() == 1);
}

TEST_CASE("invalid JSON is rejected with a message", "[model][highlight]")
{
    const auto check = [](const char* json, const char* expected) {
        const auto result = HighlightSets::fromJson(QByteArray(json));
        REQUIRE_FALSE(result.has_value());
        INFO(result.error().toStdString());
        CHECK(result.error().contains(QString::fromUtf8(expected)));
    };
    check("{", "Invalid JSON");
    check("[]", "Not a highlight set file");
    check(R"({"format":"logviewer-highlight-sets","version":99,"sets":[]})", "newer version");
    check(R"({"format":"logviewer-highlight-sets","version":1,"sets":{}})", "must be an array");
    check(R"({"format":"logviewer-highlight-sets","version":1,"sets":[{"name":"a","rules":[{}]}]})",
          "Set 1, rule 1: a rule needs a non-empty");
    check(
        R"({"format":"logviewer-highlight-sets","version":1,"sets":[{"name":"a","rules":[{"pattern":"x","scope":"word"}]}]})",
        "unknown scope");
    check(
        R"({"format":"logviewer-highlight-sets","version":1,"sets":[{"name":"a","rules":[{"pattern":"x","background":"red"}]}]})",
        "invalid color");

    // A corrupt settings entry keeps the defaults.
    const lv::test::TempDir dir;
    QSettings settings(QString::fromStdString((dir / "s.ini").string()), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Highlight/sets"), QByteArray("{broken"));
    HighlightSets sets;
    sets.load(settings);
    CHECK(sets.size() == 1);
    CHECK(sets.set(0).name == QStringLiteral("Default"));
}
