#include "log_writer.h"
#include "model/document.h"
#include "model/mark_set.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using lv::core::LineNumber;
using lv::model::Document;
using lv::model::MarkSet;

namespace
{

bool waitReady(const Document& document)
{
    QElapsedTimer timer;
    timer.start();
    while (document.state() != Document::State::kReady && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return document.state() == Document::State::kReady;
}

}  // namespace

TEST_CASE("marks toggle, navigate, and clear", "[model][marks]")
{
    const lv::test::TempDir dir;
    {
        lv::test::LogWriter writer(dir / "a.log", true);
        writer.append("a\nb\nc\nd\ne\n");
    }
    const Document document(dir / "a.log");
    MarkSet marks(document);
    const QSignalSpy changed(&marks, &MarkSet::changed);

    marks.toggle(LineNumber(3));
    marks.toggle(LineNumber(1));
    CHECK(marks.lines() == std::vector{LineNumber(1), LineNumber(3)});
    marks.toggle(LineNumber(3));  // off again
    CHECK(marks.lines() == std::vector{LineNumber(1)});

    // Several lines: mixed -> all marked; all marked -> all unmarked.
    const std::vector selection{LineNumber(0), LineNumber(1), LineNumber(2)};
    marks.toggle(selection);
    CHECK(marks.size() == 3);
    marks.toggle(selection);
    CHECK(marks.empty());

    marks.toggle(std::vector{LineNumber(1), LineNumber(4)});
    CHECK(marks.next(LineNumber(0)) == LineNumber(1));
    CHECK(marks.next(LineNumber(1)) == LineNumber(4));
    CHECK_FALSE(marks.next(LineNumber(4)));
    CHECK(marks.previous(LineNumber(4)) == LineNumber(1));
    CHECK_FALSE(marks.previous(LineNumber(1)));
    marks.clear();
    CHECK(marks.empty());
    CHECK(changed.count() == 7);
}

TEST_CASE("a new document generation clears the marks", "[model][marks]")
{
    const lv::test::TempDir dir;
    {
        lv::test::LogWriter writer(dir / "a.log", true);
        writer.append("a\nb\n");
    }
    Document document(dir / "a.log");
    MarkSet marks(document);
    document.open();
    REQUIRE(waitReady(document));
    marks.toggle(std::vector{LineNumber(0), LineNumber(1)});
    const QSignalSpy cleared(&marks, &MarkSet::cleared);
    document.reload();
    REQUIRE(cleared.count() == 1);
    CHECK(cleared.at(0).at(0).toULongLong() == 2);
    CHECK(marks.empty());
    REQUIRE(waitReady(document));
    document.reload();  // nothing to clear: no signal
    CHECK(cleared.count() == 1);
}
