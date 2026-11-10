#include "core/concurrency/thread_pool.h"
#include "core/encoding/decode.h"
#include "core/encoding/encoding.h"
#include "core/index/indexer.h"
#include "core/index/line_index.h"
#include "core/index/sorted_u64_sequence.h"
#include "core/platform/file.h"
#include "core/reader/line_reader.h"
#include "core/search/query.h"
#include "core/search/regex_matcher.h"
#include "core/search/search_engine.h"
#include "log_writer.h"
#include "loggen/loggen.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

using lv::core::CompiledQuery;
using lv::core::Encoding;
using lv::core::File;
using lv::core::LineCount;
using lv::core::LineIndex;
using lv::core::LineNumber;
using lv::core::Query;
using lv::core::QueryMode;
using lv::core::SearchEngine;
using lv::core::SearchProgress;
using lv::core::SearchRange;
using lv::core::SearchRequest;
using lv::core::ThreadPool;
using lv::test::TempDir;

namespace
{

// An indexed file ready to be searched.
struct Indexed
{
    File file;
    std::unique_ptr<LineIndex> index;

    // Indexes with a given encoding instead of the detected one (code pages, F-ENC-5).
    Indexed(const std::filesystem::path& path, lv::core::Encoding encoding)
        : file(*File::open(path))
        , index(std::make_unique<LineIndex>(encoding, 0))
    {
        REQUIRE(lv::core::Indexer::run(file, *index, std::stop_token{}).has_value());
    }

    explicit Indexed(const std::filesystem::path& path)
        : file(*File::open(path))
    {
        std::ifstream in(path, std::ios::binary);
        std::string sample(lv::core::kDetectionSampleSize, '\0');
        in.read(sample.data(), static_cast<std::streamsize>(sample.size()));
        sample.resize(static_cast<std::size_t>(in.gcount()));
        const auto detected = lv::core::detectEncoding(
            std::as_bytes(std::span(sample)), sample.size() < lv::core::kDetectionSampleSize);
        index = std::make_unique<LineIndex>(detected.encoding, detected.bomLength);
        REQUIRE(lv::core::Indexer::run(file, *index, std::stop_token{}).has_value());
    }
};

std::shared_ptr<const CompiledQuery> compile(const Query& query)
{
    auto compiled = CompiledQuery::compile(query);
    REQUIRE(compiled.has_value());
    return *compiled;
}

// Straightforward UTF-8 validation (independent of simdutf, which the engine uses).
bool isValidUtf8(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        std::uint32_t value = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0U) == 0xC0U) {
            length = 2;
            value = c & 0x1FU;
        } else if ((c & 0xF0U) == 0xE0U) {
            length = 3;
            value = c & 0x0FU;
        } else if ((c & 0xF8U) == 0xF0U) {
            length = 4;
            value = c & 0x07U;
        } else {
            return false;
        }
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            value = (value << 6U) | (next & 0x3FU);
        }
        const bool overlong = (length == 2 && value < 0x80) || (length == 3 && value < 0x800) ||
                              (length == 4 && value < 0x10000);
        if (overlong || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
            return false;
        }
        i += length;
    }
    return true;
}

// Single-threaded reference: LineReader + decodeLine + UTF-8 conversion via std::u16string.
std::vector<std::uint64_t> reference(const Indexed& data, const lv::core::Matcher& matcher,
                                     std::uint64_t first, std::uint64_t count)
{
    const lv::core::LineReader reader(data.file, *data.index);
    const auto snapshot = data.index->snapshot();
    std::vector<std::uint64_t> out;
    lv::core::MatchScratch scratch;
    constexpr std::uint64_t kBatch = 4096;
    for (std::uint64_t line = first; line < first + count; line += kBatch) {
        const std::uint64_t n = std::min(kBatch, first + count - line);
        const auto block = reader.readLines(snapshot, LineNumber(line), LineCount(n));
        REQUIRE(block.has_value());
        for (std::size_t i = 0; i < block->lines.size(); ++i) {
            const auto raw = block->line(i);
            const std::string_view bytes(reinterpret_cast<const char*>(raw.data()),  // NOLINT
                                         raw.size());
            if (data.index->encoding() == Encoding::kUtf8 && isValidUtf8(bytes)) {
                if (matcher.matches(bytes, scratch)) {
                    out.push_back(line + i);
                }
                continue;
            }
            const std::u16string text = lv::core::decodeLine(raw, data.index->encoding());
            std::string utf8;
            for (std::size_t k = 0; k < text.size(); ++k) {
                std::uint32_t c = text[k];
                if (c >= 0xD800 && c <= 0xDBFF && k + 1 < text.size()) {
                    c = 0x10000 + ((c - 0xD800) << 10U) + (text[k + 1] - 0xDC00U);
                    ++k;
                }
                if (c < 0x80) {
                    utf8 += static_cast<char>(c);
                } else if (c < 0x800) {
                    utf8 += static_cast<char>(0xC0 | (c >> 6U));
                    utf8 += static_cast<char>(0x80 | (c & 0x3FU));
                } else if (c < 0x10000) {
                    utf8 += static_cast<char>(0xE0 | (c >> 12U));
                    utf8 += static_cast<char>(0x80 | ((c >> 6U) & 0x3FU));
                    utf8 += static_cast<char>(0x80 | (c & 0x3FU));
                } else {
                    utf8 += static_cast<char>(0xF0 | (c >> 18U));
                    utf8 += static_cast<char>(0x80 | ((c >> 12U) & 0x3FU));
                    utf8 += static_cast<char>(0x80 | ((c >> 6U) & 0x3FU));
                    utf8 += static_cast<char>(0x80 | (c & 0x3FU));
                }
            }
            if (matcher.matches(utf8, scratch)) {
                out.push_back(line + i);
            }
        }
    }
    return out;
}

// Collects results and checks the prefix property on every call.
class CheckingSink final : public lv::core::ResultSink
{
public:
    void append(std::span<const std::uint64_t> lines, const SearchProgress& progress) override
    {
        for (const auto line : lines) {
            if (!matches.empty() && line <= matches.back()) {
                ordered = false;
            }
            matches.push_back(line);
        }
        if (progress.matches != matches.size() || progress.searchedLines.get() < lastSearched) {
            consistent = false;
        }
        lastSearched = progress.searchedLines.get();
        ++calls;
        if (onAppend) {
            onAppend();
        }
    }

    std::vector<std::uint64_t> matches;
    bool ordered = true;
    bool consistent = true;
    std::uint64_t lastSearched = 0;
    int calls = 0;
    std::function<void()> onAppend;
};

SearchRequest makeRequest(const Indexed& data, const lv::core::Matcher& matcher)
{
    SearchRequest request;
    request.file = &data.file;
    request.index = data.index.get();
    request.snapshot = data.index->snapshot();
    request.matcher = &matcher;
    return request;
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    lv::test::LogWriter writer(path, true);
    writer.append(text);
}

}  // namespace

namespace
{

void compareWithReference(lv::loggen::Profile profile, std::uint64_t sizeBytes,
                          const std::vector<Query>& queries)
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.profile = profile;
    write.sizeBytes = sizeBytes;
    write.out = dir / "gen.log";
    REQUIRE(lv::loggen::writeLog(write).ok());
    const Indexed data(write.out);
    const auto snapshot = data.index->snapshot();
    for (const auto& query : queries) {
        const auto matcher = compile(query);
        const auto expected = reference(data, *matcher, 0, snapshot.lineCount().get());
        for (const std::size_t threads : {1U, 2U, 8U}) {
            CAPTURE(lv::loggen::profileName(profile), query.text, threads);
            ThreadPool pool(threads);
            CheckingSink sink;
            SearchRequest request = makeRequest(data, *matcher);
            request.options.chunkBytes = std::size_t{256} * 1024;
            const auto stats = SearchEngine::run(request, pool, std::stop_token{}, sink);
            REQUIRE(stats.has_value());
            CHECK(sink.ordered);
            CHECK(sink.consistent);
            CHECK(sink.matches == expected);
            CHECK(stats->matches == expected.size());
            CHECK(stats->searchedLines == snapshot.lineCount());
            CHECK(stats->chunks > 1);
        }
    }
}

}  // namespace

TEST_CASE("search equals a single-threaded reference on generated logs", "[fs][search]")
{
    const auto profile = GENERATE(lv::loggen::Profile::kSmall, lv::loggen::Profile::kShort,
                                  lv::loggen::Profile::kUtf16, lv::loggen::Profile::kCp1252);
    compareWithReference(profile, std::uint64_t{20} * 1024 * 1024,
                         {{.text = "ERROR"},
                          {.text = R"((ERROR|WARN).*timeout)", .mode = QueryMode::kRegex},
                          {.text = R"(/ERROR/ AND NOT "heartbeat")", .mode = QueryMode::kBoolean}});
}

TEST_CASE("search options equal the reference on smaller logs", "[fs][search]")
{
    const auto profile = GENERATE(lv::loggen::Profile::kUtf16, lv::loggen::Profile::kCp1252);
    compareWithReference(profile, std::uint64_t{2} * 1024 * 1024,
                         {{.text = "größe"},
                          {.text = "INFO", .invert = true},
                          {.text = "error", .caseSensitive = true, .wholeWord = true}});
}

TEST_CASE("search handles edge cases and ranges", "[fs][search]")
{
    const TempDir dir;
    const auto chunk = GENERATE(std::size_t{1}, std::size_t{7}, std::size_t{4096});
    const std::vector<std::string> inputs{
        "", "a", "a\n", "\n\na\n", "a\r\nb\r\na", "x\na\nb\na", std::string("\xFF") + "a\nb\xC3"};
    ThreadPool pool(3);
    const auto matcher = compile({.text = "a"});
    for (const auto& text : inputs) {
        CAPTURE(text, chunk);
        writeText(dir / "e.log", text);
        const Indexed data(dir / "e.log");
        const auto snapshot = data.index->snapshot();
        const auto total = snapshot.lineCount().get();
        for (std::uint64_t first = 0; first <= total; ++first) {
            for (std::uint64_t count = 0; first + count <= total; ++count) {
                CheckingSink sink;
                SearchRequest request = makeRequest(data, *matcher);
                request.range = SearchRange{.first = LineNumber(first), .count = LineCount(count)};
                request.options.chunkBytes = chunk;
                const auto stats = SearchEngine::run(request, pool, std::stop_token{}, sink);
                REQUIRE(stats.has_value());
                CHECK(sink.matches == reference(data, *matcher, first, count));
                CHECK(stats->searchedLines.get() == count);
            }
        }
    }
}

TEST_CASE("cancellation leaves a consistent ordered prefix", "[fs][search]")
{
    const TempDir dir;
    lv::loggen::WriteOptions write;
    write.sizeBytes = std::uint64_t{8} * 1024 * 1024;
    write.out = dir / "gen.log";
    REQUIRE(lv::loggen::writeLog(write).ok());
    const Indexed data(write.out);
    const auto snapshot = data.index->snapshot();
    const auto matcher = compile({.text = "e"});
    const auto expected = reference(data, *matcher, 0, snapshot.lineCount().get());

    ThreadPool pool(4);
    std::stop_source stop;
    CheckingSink sink;
    sink.onAppend = [&] {
        if (sink.calls == 3) {
            stop.request_stop();
        }
    };
    SearchRequest request = makeRequest(data, *matcher);
    request.options.chunkBytes = std::size_t{64} * 1024;
    const auto result = SearchEngine::run(request, pool, stop.get_token(), sink);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == lv::core::ErrorCode::kCanceled);
    CHECK(sink.calls == 3);
    CHECK(sink.ordered);
    REQUIRE(sink.matches.size() <= expected.size());
    CHECK(std::equal(sink.matches.begin(), sink.matches.end(), expected.begin()));
}

TEST_CASE("a file shorter than its index is reported", "[fs][search]")
{
    const TempDir dir;
    std::string text;
    for (int i = 0; i < 1000; ++i) {
        text += "line " + std::to_string(i) + "\n";
    }
    writeText(dir / "t.log", text);
    const Indexed data(dir / "t.log");
    std::filesystem::resize_file(dir / "t.log", text.size() / 2);
    ThreadPool pool(2);
    const auto matcher = compile({.text = "line"});
    CheckingSink sink;
    SearchRequest request = makeRequest(data, *matcher);
    request.options.chunkBytes = 512;
    const auto result = SearchEngine::run(request, pool, std::stop_token{}, sink);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == lv::core::ErrorCode::kOutOfRange);
    CHECK(sink.ordered);
    CHECK(sink.matches.size() < 1000);
    for (std::size_t i = 0; i < sink.matches.size(); ++i) {
        CHECK(sink.matches[i] == i);  // prefix of all lines
    }
}

TEST_CASE("skipped lines are counted and the sequence sink publishes a prefix", "[fs][search]")
{
    const TempDir dir;
    std::string text;
    for (int i = 0; i < 50; ++i) {
        text += (i % 10 == 0 ? std::string(3000, 'a') + "b" : std::string("ok")) + "\n";
    }
    writeText(dir / "s.log", text);
    const Indexed data(dir / "s.log");
    auto regex =
        lv::core::RegexMatcher::compile("(a+)+$", {.caseSensitive = true, .matchLimit = 10000});
    REQUIRE(regex.has_value());
    ThreadPool pool(2);
    lv::core::SortedU64Sequence matches;
    lv::core::SequenceSink sink(matches);
    const SearchRequest request = makeRequest(data, **regex);
    const auto stats = SearchEngine::run(request, pool, std::stop_token{}, sink);
    REQUIRE(stats.has_value());
    CHECK(stats->skippedLines == 5);
    CHECK(sink.progress().skippedLines == 5);
    CHECK(sink.progress().searchedLines.get() == 50);
    CHECK(matches.empty());
}

TEST_CASE("code page files are searched by their decoded text", "[fs][search]")
{
    // F-ENC-5: non-ASCII bytes are decoded before matching ("мир" in Windows-1251,
    // "žluť" in Windows-1250, "€" in ISO-8859-15).
    const TempDir dir;
    ThreadPool pool(2);

    struct Case
    {
        lv::core::Encoding encoding;
        std::string text;
        std::string query;  // UTF-8
        std::vector<std::uint64_t> expected;
    };

    const std::vector<Case> cases{
        // Case-insensitive by default: both lines match.
        {.encoding = lv::core::Encoding::kWindows1251,
         .text = "ok\n\xEC\xE8\xF0 1\nx\n\xCC\xC8\xD0 2\n",
         .query = "\xD0\xBC\xD0\xB8\xD1\x80",
         .expected = {1, 3}},
        {.encoding = lv::core::Encoding::kWindows1250,
         .text = "a\n\x9Elu\x9D\nb\n",
         .query = "\xC5\xBElu\xC5\xA5",
         .expected = {1}},
        {.encoding = lv::core::Encoding::kIso885915,
         .text = "price \xA4 5\nnone\n",
         .query = "\xE2\x82\xAC",
         .expected = {0}},
    };
    for (const Case& test : cases) {
        CAPTURE(lv::core::displayName(test.encoding));
        writeText(dir / "cp.log", test.text);
        const Indexed data(dir / "cp.log", test.encoding);
        const auto matcher = compile({.text = test.query});
        CheckingSink sink;
        const SearchRequest request = makeRequest(data, *matcher);
        REQUIRE(SearchEngine::run(request, pool, std::stop_token{}, sink).has_value());
        CHECK(sink.matches == test.expected);
        CHECK(sink.matches ==
              reference(data, *matcher, 0, data.index->snapshot().lineCount().get()));
    }
}
