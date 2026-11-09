#include "model/highlight_sets.h"
#include "model/line_highlighter.h"

#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using lv::core::LineNumber;
using lv::model::HighlightRule;
using lv::model::HighlightScope;
using lv::model::HighlightSet;
using lv::model::HighlightSets;
using lv::model::LineHighlighter;

namespace
{

HighlightSet setWith(const char* name, const char* pattern, lv::model::Argb background)
{
    return HighlightSet{.name = QString::fromUtf8(name),
                        .rules = {HighlightRule{.pattern = QString::fromUtf8(pattern),
                                                .regex = false,
                                                .caseSensitive = false,
                                                .scope = HighlightScope::kWholeLine,
                                                .foreground = std::nullopt,
                                                .background = background,
                                                .enabled = true}}};
}

}  // namespace

TEST_CASE("the tab's active set follows the default or a chosen set", "[model][highlight]")
{
    HighlightSets sets;
    sets.replace(0, setWith("A", "x", 0xFF0000AAU));
    const std::size_t b = sets.add(setWith("B", "x", 0xFF0000BBU));
    LineHighlighter highlighter;
    CHECK(highlighter.highlight(LineNumber(0), QStringLiteral("x"), false).empty());  // no sets
    highlighter.setSets(&sets);
    const QSignalSpy changed(&highlighter, &LineHighlighter::changed);

    CHECK(highlighter.highlight(LineNumber(0), QStringLiteral("x"), false).background ==
          0xFF0000AAU);
    CHECK(highlighter.cacheSize() == 1);
    // Cached per line: the same line number returns the cached result.
    CHECK(highlighter.highlight(LineNumber(0), QStringLiteral("y"), false).background ==
          0xFF0000AAU);
    // Partial lines are not cached.
    CHECK(highlighter.highlight(LineNumber(1), QStringLiteral("y"), true).empty());
    CHECK(highlighter.cacheSize() == 1);

    // Changing the default repaints a tab that follows it.
    sets.setDefaultIndex(b);
    CHECK(changed.count() == 1);
    CHECK(highlighter.cacheSize() == 0);
    CHECK(highlighter.highlight(LineNumber(0), QStringLiteral("x"), false).background ==
          0xFF0000BBU);

    // A chosen set is independent of the default.
    highlighter.setActiveSet(0);
    CHECK(highlighter.activeSet() == 0);
    CHECK(highlighter.highlight(LineNumber(0), QStringLiteral("x"), false).background ==
          0xFF0000AAU);
    const auto before = changed.count();
    sets.setDefaultIndex(0);
    sets.setDefaultIndex(b);
    CHECK(changed.count() == before);  // not affected

    // Editing the active set repaints; editing another set does not.
    sets.replace(b, setWith("B", "z", 0xFF0000BBU));
    CHECK(changed.count() == before);
    sets.replace(0, setWith("A", "x", 0xFF0000CCU));
    CHECK(changed.count() == before + 1);
    CHECK(highlighter.highlight(LineNumber(0), QStringLiteral("x"), false).background ==
          0xFF0000CCU);

    // Removing the chosen set falls back to the default.
    highlighter.setActiveSet(b);
    REQUIRE(sets.remove(b));
    CHECK_FALSE(highlighter.chosenSet());
    CHECK(highlighter.activeSet() == sets.defaultIndex());
}

TEST_CASE("a chosen set is kept by name across reordering", "[model][highlight]")
{
    HighlightSets sets;
    sets.assign({setWith("A", "x", 0xFF0000AAU), setWith("B", "x", 0xFF0000BBU)}, 0);
    LineHighlighter highlighter;
    highlighter.setSets(&sets);
    highlighter.setActiveSet(1);  // "B"
    sets.assign({setWith("B", "x", 0xFF0000BBU), setWith("A", "x", 0xFF0000AAU)}, 1);
    CHECK(highlighter.chosenSet() == 0);
    CHECK(highlighter.highlight(LineNumber(0), QStringLiteral("x"), false).background ==
          0xFF0000BBU);
    // Renamed: back to the default ("A").
    sets.replace(0, setWith("C", "x", 0xFF0000CCU));
    CHECK_FALSE(highlighter.chosenSet());
    CHECK(highlighter.activeSet() == 1);
}
