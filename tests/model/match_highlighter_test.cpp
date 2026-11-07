#include "core/search/query.h"
#include "model/match_highlighter.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using lv::core::CompiledQuery;
using lv::core::MatchScratch;
using lv::core::Query;
using lv::core::QueryMode;
using lv::model::HighlightRange;
using lv::model::highlightRanges;

namespace
{

std::vector<HighlightRange> ranges(const Query& query, const QString& text)
{
    const auto compiled = CompiledQuery::compile(query);
    REQUIRE(compiled.has_value());
    MatchScratch scratch;
    return highlightRanges(**compiled, text, scratch);
}

}  // namespace

TEST_CASE("highlight ranges are UTF-16 columns", "[model][highlight]")
{
    CHECK(ranges({.text = "err"}, QStringLiteral("x ERR y err")) ==
          std::vector<HighlightRange>{{2, 5}, {8, 11}});
    // Umlauts are two bytes in UTF-8 but one column.
    CHECK(ranges({.text = "größe"}, QStringLiteral("Die GRÖßE: größe")) ==
          std::vector<HighlightRange>{{4, 9}, {11, 16}});
    // Characters outside the BMP take two UTF-16 units.
    CHECK(ranges({.text = "x"}, QString::fromUtf8("😀x😀x")) ==
          std::vector<HighlightRange>{{2, 3}, {5, 6}});
    CHECK(ranges({.text = "\\t\\d", .mode = QueryMode::kRegex}, QStringLiteral("a\t1\t2")) ==
          std::vector<HighlightRange>{{1, 3}, {3, 5}});
    CHECK(ranges({.text = "x", .invert = true}, QStringLiteral("x")).empty());  // nothing to mark
    CHECK(ranges({.text = R"("a" AND NOT "b")", .mode = QueryMode::kBoolean},
                 QStringLiteral("a b a")) == std::vector<HighlightRange>{{0, 1}, {4, 5}});
}
