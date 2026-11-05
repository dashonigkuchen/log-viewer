#include "core/encoding/decode.h"
#include "log_writer.h"
#include "model/document.h"
#include "model/document_line_source.h"
#include "model/line_cache.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>

using lv::core::LineCount;
using lv::core::LineNumber;
using lv::model::DisplayLine;
using lv::model::Document;
using lv::model::DocumentLineSource;
using lv::model::LineCache;
using lv::test::TempDir;

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
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    lv::test::LogWriter writer(path, true);
    writer.append(text);
}

QString decoded(std::string_view raw, lv::core::Encoding encoding)
{
    const auto text = lv::core::decodeLine(std::as_bytes(std::span(raw.data(), raw.size())),
                                           encoding, {.visibleControls = true});
    return QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

TEST_CASE("LineCache hit, miss, and LRU eviction", "[model][cache]")
{
    LineCache cache(3);
    cache.setGeneration(1);
    CHECK_FALSE(cache.find(LineNumber(0)).has_value());
    cache.insert(LineNumber(0), DisplayLine{.text = "a"});
    cache.insert(LineNumber(1), DisplayLine{.text = "b"});
    cache.insert(LineNumber(2), DisplayLine{.text = "c"});
    REQUIRE(cache.find(LineNumber(0)).has_value());         // 0 becomes most recently used
    cache.insert(LineNumber(3), DisplayLine{.text = "d"});  // evicts 1
    CHECK(cache.size() == 3);
    CHECK_FALSE(cache.find(LineNumber(1)).has_value());
    CHECK(cache.find(LineNumber(0)).value_or(DisplayLine{}).text == "a");
    CHECK(cache.find(LineNumber(3)).value_or(DisplayLine{}).text == "d");
    CHECK(cache.hits() == 3);
    CHECK(cache.misses() == 2);

    cache.insert(LineNumber(3), DisplayLine{.text = "D"});
    CHECK(cache.find(LineNumber(3)).value_or(DisplayLine{}).text == "D");
    CHECK(cache.size() == 3);
}

TEST_CASE("LineCache drops entries on a generation change", "[model][cache]")
{
    LineCache cache;
    cache.setGeneration(1);
    cache.insert(LineNumber(5), DisplayLine{.text = "x"});
    cache.setGeneration(1);
    CHECK(cache.size() == 1);
    cache.setGeneration(2);
    CHECK(cache.size() == 0);
    CHECK_FALSE(cache.find(LineNumber(5)).has_value());
}

TEST_CASE("DocumentLineSource decodes like decodeLine and caches", "[model][source]")
{
    const TempDir dir;
    const auto path = dir / "a.log";
    const std::array<std::string, 4> lines{"plain",
                                           "Gr\xC3\xB6\xC3\x9F"
                                           "e\t\xE2\x82\xAC",
                                           std::string("nul\0x", 5), "bad \xFF"};
    std::string text;
    for (const auto& line : lines) {
        text += line + "\r\n";
    }
    text += "tail";
    writeText(path, text);

    Document document(path);
    const DocumentLineSource source(document);
    // The invalid byte would make detection choose Windows-1252; force UTF-8 to test U+FFFD.
    document.setEncodingOverride(lv::core::Encoding::kUtf8);
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    REQUIRE(source.lineCount().get() == 5);
    CHECK(source.originalLineNumber(LineNumber(3)) == LineNumber(3));

    const auto rows = source.lines(LineNumber(0), LineCount(5));
    REQUIRE(rows.size() == 5);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(rows[i].text == decoded(lines.at(i), lv::core::Encoding::kUtf8));
        CHECK_FALSE(rows[i].partial);
    }
    CHECK(rows[2].text == QString::fromUtf16(u"nul\u2400x"));
    CHECK(rows[4].text == "tail");
    CHECK(rows[4].partial);

    // Complete lines are cached, the partial tail is not.
    CHECK(source.cache().size() == 4);
    const auto missesBefore = source.cache().misses();
    const auto again = source.lines(LineNumber(1), LineCount(2));
    CHECK(again[0].text == rows[1].text);
    CHECK(source.cache().misses() == missesBefore);
    CHECK(source.cache().hits() >= 2);
}

TEST_CASE("DocumentLineSource is invalidated on reload and encoding change", "[model][source]")
{
    const TempDir dir;
    const auto path = dir / "b.log";
    writeText(path, "Gr\xC3\xB6\xC3\x9F"
                    "e\nsecond\n");
    Document document(path);
    DocumentLineSource source(document);
    const QSignalSpy resets(&source, &DocumentLineSource::reset);
    const QSignalSpy appended(&source, &DocumentLineSource::linesAppended);
    document.open();
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    CHECK(source.lines(LineNumber(0), LineCount(1))[0].text == QString::fromUtf8("Größe"));
    CHECK(appended.count() >= 1);

    document.setEncodingOverride(lv::core::Encoding::kWindows1252);
    CHECK(source.cache().size() == 0);
    REQUIRE(waitFor([&] { return document.state() == Document::State::kReady; }));
    CHECK(source.lines(LineNumber(0), LineCount(1))[0].text == QString::fromUtf8("GrÃ¶ÃŸe"));
    CHECK(resets.count() == 2);
}

TEST_CASE("DocumentLineSource before the document is open", "[model][source]")
{
    const TempDir dir;
    const Document document(dir / "missing.log");
    const DocumentLineSource source(document);
    CHECK(source.lineCount().get() == 0);
    CHECK(source.lines(LineNumber(0), LineCount(0)).empty());
}
