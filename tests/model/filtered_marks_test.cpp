#include "core/concurrency/thread_pool.h"
#include "log_writer.h"
#include "model/document.h"
#include "model/document_line_source.h"
#include "model/filtered_document.h"
#include "model/filtered_line_source.h"
#include "model/mark_set.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using lv::core::LineCount;
using lv::core::LineNumber;
using lv::core::Query;
using lv::model::Document;
using lv::model::DocumentLineSource;
using lv::model::FilteredDocument;
using lv::model::FilteredLineSource;
using lv::model::MarkSet;

namespace
{

template <typename Condition>
bool waitFor(Condition condition, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

std::vector<std::uint64_t> rowLines(const FilteredLineSource& source)
{
    std::vector<std::uint64_t> out;
    for (std::uint64_t row = 0; row < source.lineCount().get(); ++row) {
        out.push_back(source.originalLineNumber(LineNumber(row)).get());
    }
    return out;
}

std::vector<bool> extraFlags(const FilteredLineSource& source)
{
    std::vector<bool> out;
    for (const auto& line : source.lines(LineNumber(0), source.lineCount())) {
        out.push_back(line.extra);
    }
    return out;
}

}  // namespace

TEST_CASE("marked lines are merged into the filtered rows", "[model][filter][marks]")
{
    const lv::test::TempDir dir;
    {
        // 0 INFO, 1 ERROR, 2 INFO, 3 ERROR, 4 INFO, 5 INFO
        lv::test::LogWriter writer(dir / "a.log", true);
        writer.append("INFO 0\nERROR 1\nINFO 2\nERROR 3\nINFO 4\nINFO 5\n");
    }
    lv::core::ThreadPool pool(2);
    Document document(dir / "a.log");
    const DocumentLineSource lines(document);
    FilteredDocument filter(document, pool);
    MarkSet marks(document);
    FilteredLineSource source(filter, lines);
    source.setMarks(&marks);
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    REQUIRE_FALSE(filter.apply(Query{.text = "ERROR"}).has_value());
    REQUIRE(waitFor([&] { return filter.state() == FilteredDocument::State::kDone; }));
    CHECK(rowLines(source) == std::vector<std::uint64_t>{1, 3});

    const QSignalSpy about(&source, &FilteredLineSource::rowsAboutToChange);
    const QSignalSpy changed(&source, &FilteredLineSource::rowsChanged);
    marks.toggle(std::vector{LineNumber(0), LineNumber(3), LineNumber(5)});
    CHECK(about.count() == 1);
    CHECK(changed.count() == 1);
    // In line order; line 3 is a match and appears once.
    CHECK(rowLines(source) == std::vector<std::uint64_t>{0, 1, 3, 5});
    CHECK(extraFlags(source) == std::vector<bool>{true, false, false, true});
    CHECK(source.isExtraRow(LineNumber(0)));
    CHECK_FALSE(source.isExtraRow(LineNumber(2)));
    CHECK(source.lines(LineNumber(3), LineCount(1)).front().text == QStringLiteral("INFO 5"));
    CHECK(source.rowsBefore(LineNumber(3)) == 2);
    CHECK(source.rowsBefore(LineNumber(6)) == 4);
    const auto reader = source.rowReader();
    REQUIRE(reader.has_value());
    if (reader) {
        CHECK(reader->lineForRow(LineNumber(3)).get() == 5);
        CHECK(reader->lineForRow(LineNumber(1)).get() == 1);
    }

    // Unmarking removes the extra row; the match stays.
    marks.toggle(LineNumber(0));
    marks.toggle(LineNumber(3));
    CHECK(rowLines(source) == std::vector<std::uint64_t>{1, 3, 5});

    // Option off: only matches.
    source.setShowMarks(false);
    CHECK(rowLines(source) == std::vector<std::uint64_t>{1, 3});
    source.setShowMarks(true);
    CHECK(rowLines(source) == std::vector<std::uint64_t>{1, 3, 5});

    // A new filter keeps the marks merged.
    REQUIRE_FALSE(filter.apply(Query{.text = "INFO 2"}).has_value());
    REQUIRE(waitFor([&] { return filter.state() == FilteredDocument::State::kDone; }));
    CHECK(rowLines(source) == std::vector<std::uint64_t>{2, 5});
}
