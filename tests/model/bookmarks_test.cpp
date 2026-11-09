#include "log_writer.h"
#include "model/bookmarks.h"
#include "model/document.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using lv::core::LineNumber;
using lv::model::Bookmarks;
using lv::model::Document;

TEST_CASE("bookmarks add, rename, remove, and navigate", "[model][bookmarks]")
{
    const lv::test::TempDir dir;
    {
        lv::test::LogWriter writer(dir / "a.log", true);
        writer.append("a\nb\nc\n");
    }
    const Document document(dir / "a.log");
    Bookmarks bookmarks(document);
    const QSignalSpy changed(&bookmarks, &Bookmarks::changed);

    CHECK(bookmarks.toggle(LineNumber(2), QStringLiteral("  2026-11-09   ERROR\tdisk full  ")));
    CHECK(bookmarks.name(LineNumber(2)) == QStringLiteral("2026-11-09 ERROR disk full"));
    bookmarks.add(LineNumber(0), QStringLiteral("start"));
    REQUIRE(bookmarks.size() == 2);
    CHECK(bookmarks.entries().front().line == LineNumber(0));
    CHECK(bookmarks.entries().back().name == QStringLiteral("2026-11-09 ERROR disk full"));

    bookmarks.rename(LineNumber(0), QStringLiteral("begin"));
    CHECK(bookmarks.name(LineNumber(0)) == QStringLiteral("begin"));
    bookmarks.rename(LineNumber(1), QStringLiteral("x"));  // no bookmark: ignored
    CHECK_FALSE(bookmarks.contains(LineNumber(1)));

    CHECK(bookmarks.next(LineNumber(0)) == LineNumber(2));
    CHECK_FALSE(bookmarks.next(LineNumber(2)));
    CHECK(bookmarks.previous(LineNumber(2)) == LineNumber(0));
    CHECK_FALSE(bookmarks.previous(LineNumber(0)));

    CHECK_FALSE(bookmarks.toggle(LineNumber(2), QString()));  // removes
    bookmarks.remove(LineNumber(0));
    CHECK(bookmarks.empty());
    CHECK(changed.count() == 5);
}

TEST_CASE("default bookmark names are cut to 60 characters", "[model][bookmarks]")
{
    const QString longLine(100, u'x');
    CHECK(Bookmarks::defaultName(longLine).size() == Bookmarks::kDefaultNameLength);
    CHECK(Bookmarks::defaultName(QString()).isEmpty());
}

TEST_CASE("a new document generation clears the bookmarks", "[model][bookmarks]")
{
    const lv::test::TempDir dir;
    {
        lv::test::LogWriter writer(dir / "a.log", true);
        writer.append("a\nb\n");
    }
    Document document(dir / "a.log");
    Bookmarks bookmarks(document);
    document.open();
    QElapsedTimer timer;
    timer.start();
    while (document.state() != Document::State::kReady && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    REQUIRE(document.state() == Document::State::kReady);
    bookmarks.add(LineNumber(1), QStringLiteral("b"));
    const QSignalSpy cleared(&bookmarks, &Bookmarks::cleared);
    document.reload();
    REQUIRE(cleared.count() == 1);
    CHECK(cleared.at(0).at(0).toULongLong() == 1);
    CHECK(bookmarks.empty());
}
