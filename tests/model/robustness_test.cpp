// Robustness suite (SPEC F-ROB-1, F-ROB-2): hostile inputs and I/O errors must never crash the
// application, and errors in workers end up in the tab. Runs under ASan/UBSan in CI.

#include "core/concurrency/thread_pool.h"
#include "core/encoding/decode.h"
#include "core/platform/file.h"
#include "log_writer.h"
#include "model/document.h"
#include "model/document_line_source.h"
#include "model/filtered_document.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <new>
#include <random>
#include <span>
#include <string>
#include <vector>

using lv::core::ByteOffset;
using lv::core::File;
using lv::core::LineCount;
using lv::core::LineNumber;
using lv::model::Document;
using lv::model::DocumentLineSource;
using lv::model::FilteredDocument;
using lv::test::TempDir;

namespace
{

template <typename Condition>
bool waitFor(Condition condition, int timeoutMs = 20000)
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

void writeBytes(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string randomBytes(std::size_t size, std::uint32_t seed)
{
    std::mt19937 random(seed);
    std::uniform_int_distribution<int> byte(0, 255);
    std::string out(size, '\0');
    for (char& c : out) {
        c = static_cast<char>(byte(random));
    }
    return out;
}

// Removes the fault hook even if a check fails.
struct FaultHookGuard
{
    FaultHookGuard() = default;
    FaultHookGuard(const FaultHookGuard&) = delete;
    FaultHookGuard& operator=(const FaultHookGuard&) = delete;
    FaultHookGuard(FaultHookGuard&&) = delete;
    FaultHookGuard& operator=(FaultHookGuard&&) = delete;

    ~FaultHookGuard() { File::setReadFaultHook({}); }
};

lv::core::Error ioError()
{
    return lv::core::Error{
        .code = lv::core::ErrorCode::kIoError, .systemCode = 5, .message = "injected I/O error"};
}

// Opens `path`, waits until it is ready, reads every line through the display source (which
// decodes with visible control characters), and filters for `query`.
struct Opened
{
    Document document;
    DocumentLineSource source;
    lv::core::ThreadPool pool{2};
    FilteredDocument filter;

    explicit Opened(const std::filesystem::path& path)
        : document(path)
        , source(document)
        , filter(document, pool)
    {
        document.open();
    }

    bool ready()
    {
        return waitFor([this] {
            return document.state() == Document::State::kReady ||
                   document.state() == Document::State::kError;
        });
    }

    // Reads all lines (in pages, like the view) and returns the number of rows read.
    std::uint64_t readAll() const
    {
        const std::uint64_t lines = document.lineCount().get();
        for (std::uint64_t first = 0; first < lines; first += 50) {
            const std::uint64_t count = std::min<std::uint64_t>(50, lines - first);
            REQUIRE(source.lines(LineNumber(first), LineCount(count)).size() == count);
        }
        return lines;
    }
};

}  // namespace

TEST_CASE("hostile files open, display, and filter without crashing", "[model][robust]")
{
    const TempDir dir;

    struct Case
    {
        const char* name;
        std::string bytes;
        std::uint64_t lines;
    };

    std::string nulLines;
    for (int i = 0; i < 100; ++i) {
        nulLines += std::string("a\0b\0", 4) + std::to_string(i) + "\n";
    }
    const auto c = GENERATE(as<int>{}, 0, 1, 2, 3, 4, 5, 6);
    const std::vector<Case> cases{
        {.name = "empty", .bytes = "", .lines = 0},
        {.name = "only CR", .bytes = std::string(10000, '\r'), .lines = 1},
        {.name = "NUL bytes", .bytes = nulLines, .lines = 100},
        {.name = "only NUL", .bytes = std::string(100000, '\0'), .lines = 1},
        {.name = "binary", .bytes = randomBytes(std::size_t{2} * 1024 * 1024, 42), .lines = 0},
        {.name = "invalid UTF-8",
         .bytes = std::string("ok\n\xC3\n\xED\xA0\x80\n\xF4\x90\x80\x80\n"),
         .lines = 4},
        {.name = "BOM only", .bytes = std::string("\xEF\xBB\xBF"), .lines = 0},
    };
    const Case& test = cases.at(static_cast<std::size_t>(c));
    CAPTURE(test.name);
    writeBytes(dir / "hostile.log", test.bytes);
    Opened opened(dir / "hostile.log");
    REQUIRE(opened.ready());
    REQUIRE(opened.document.state() == Document::State::kReady);
    if (test.lines > 0) {
        CHECK(opened.document.lineCount().get() == test.lines);
    }
    opened.readAll();

    REQUIRE_FALSE(opened.filter.apply({.text = "a"}).has_value());
    REQUIRE(waitFor([&] { return opened.filter.state() == FilteredDocument::State::kDone; }));
    REQUIRE_FALSE(
        opened.filter.apply({.text = "(a|b)+$", .mode = lv::core::QueryMode::kRegex}).has_value());
    REQUIRE(waitFor([&] { return opened.filter.state() == FilteredDocument::State::kDone; }));
}

TEST_CASE("binary bytes are shown as visible placeholders", "[model][robust]")
{
    // F-ROB-1: control characters become U+2400.. (Control Pictures), DEL becomes U+2421.
    std::u16string text;
    const std::string raw("a\0b\x01\x1B\x7F", 6);
    lv::core::decodeLine(std::as_bytes(std::span(raw)), lv::core::Encoding::kWindows1252, text,
                         nullptr, {.visibleControls = true});
    CHECK(text == u"a\u2400b\u2401\u241B\u2421");
}

TEST_CASE("a single 16 MiB line is cut, not loaded unbounded", "[model][robust]")
{
    const TempDir dir;
    const std::size_t size = (std::size_t{16} * 1024 * 1024) + 4096;
    writeBytes(dir / "long.log", std::string(size, 'x') + "\nshort\n");
    Opened opened(dir / "long.log");
    REQUIRE(opened.ready());
    REQUIRE(opened.document.state() == Document::State::kReady);
    CHECK(opened.document.lineCount().get() == 2);
    const auto out = opened.source.lines(LineNumber(0), LineCount(2));
    REQUIRE(out.size() == 2);
    CHECK(out[0].truncated);
    CHECK_FALSE(out[1].truncated);
    CHECK(out[1].text == QStringLiteral("short"));
    // Searching the long line works too.
    REQUIRE_FALSE(opened.filter.apply({.text = "short"}).has_value());
    REQUIRE(waitFor([&] { return opened.filter.state() == FilteredDocument::State::kDone; }));
    CHECK(opened.filter.matchCount() == 1);
}

TEST_CASE("I/O errors while indexing show an error in the tab", "[model][robust]")
{
    const TempDir dir;
    std::string text;
    for (int i = 0; i < 100000; ++i) {
        text += "line " + std::to_string(i) + " with some padding text\n";  // ~3 MiB
    }
    writeBytes(dir / "io.log", text);
    const FaultHookGuard guard;
    const auto target = dir / "io.log";
    // Fail every read past 1 MiB (indexing reads 512 KiB blocks): detection and the first
    // blocks work, indexing fails part way.
    File::setReadFaultHook([target](const std::filesystem::path& path, ByteOffset offset,
                                    std::size_t) -> std::optional<lv::core::Error> {
        if (path == target && offset.get() >= std::uint64_t{1024} * 1024) {
            return ioError();
        }
        return std::nullopt;
    });
    Opened opened(dir / "io.log");
    REQUIRE(opened.ready());
    CHECK(opened.document.state() == Document::State::kError);
    CHECK(opened.document.errorMessage().contains(QStringLiteral("injected")));

    // After the fault is gone, a reload recovers.
    File::setReadFaultHook({});
    opened.document.reload();
    REQUIRE(waitFor([&] { return opened.document.state() == Document::State::kReady; }));
    CHECK(opened.document.lineCount().get() == 100000);
}

TEST_CASE("I/O errors while reading lines and filtering are reported", "[model][robust]")
{
    const TempDir dir;
    std::string text;
    for (int i = 0; i < 5000; ++i) {
        text += (i % 7 == 0 ? "ERROR " : "info ") + std::to_string(i) + "\n";
    }
    writeBytes(dir / "io.log", text);
    Opened opened(dir / "io.log");
    REQUIRE(opened.ready());
    REQUIRE(opened.document.state() == Document::State::kReady);

    const FaultHookGuard guard;
    std::atomic<int> faults{0};
    File::setReadFaultHook([&faults](const std::filesystem::path&, ByteOffset,
                                     std::size_t) -> std::optional<lv::core::Error> {
        ++faults;
        return ioError();
    });
    // The view shows placeholders for lines it cannot read.
    const auto out = opened.source.lines(LineNumber(4000), LineCount(10));
    REQUIRE(out.size() == 10);
    CHECK(out[0].unavailable);

    // The filter fails with an error state.
    REQUIRE_FALSE(opened.filter.apply({.text = "ERROR"}).has_value());
    REQUIRE(waitFor([&] { return opened.filter.state() == FilteredDocument::State::kError; }));
    CHECK(opened.filter.errorMessage().contains(QStringLiteral("injected")));
    CHECK(faults.load() > 0);

    // Once the reads work again, the same filter succeeds.
    File::setReadFaultHook({});
    REQUIRE_FALSE(opened.filter.apply({.text = "ERROR"}).has_value());
    REQUIRE(waitFor([&] { return opened.filter.state() == FilteredDocument::State::kDone; }));
    CHECK(opened.filter.matchCount() == (5000 + 6) / 7);
}

TEST_CASE("a file that disappears while it is indexed does not crash", "[model][robust]")
{
    const TempDir dir;
    std::string text;
    for (int i = 0; i < 200000; ++i) {
        text += "line " + std::to_string(i) + " some more text to make it larger\n";
    }
    writeBytes(dir / "gone.log", text);
    const auto path = dir / "gone.log";
    const FaultHookGuard guard;
    std::atomic<bool> removed{false};
    // Delete the file during the first indexing read past 1 MiB (simulates a cleanup job).
    File::setReadFaultHook([path, &removed](const std::filesystem::path& file, ByteOffset offset,
                                            std::size_t) -> std::optional<lv::core::Error> {
        if (file == path && offset.get() > std::uint64_t{1024} * 1024 && !removed.exchange(true)) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
        return std::nullopt;
    });
    Opened opened(path);
    REQUIRE(opened.ready());
    // POSIX keeps the open file readable; Windows (full sharing) also allows the delete. Either
    // way the document ends in a defined state and stays usable.
    CHECK((opened.document.state() == Document::State::kReady ||
           opened.document.state() == Document::State::kError));
    opened.readAll();
    CHECK(removed.load());
}

TEST_CASE("running out of memory in the workers shows an error, no crash", "[model][robust]")
{
    const TempDir dir;
    std::string text;
    for (int i = 0; i < 100000; ++i) {
        text += (i % 7 == 0 ? "ERROR " : "info ") + std::to_string(i) + " padding text\n";
    }
    writeBytes(dir / "oom.log", text);
    const auto target = dir / "oom.log";
    const FaultHookGuard guard;
    std::atomic<bool> fail{true};
    // std::bad_alloc thrown from a read simulates an allocation failure in the worker.
    File::setReadFaultHook([target, &fail](const std::filesystem::path& path, ByteOffset offset,
                                           std::size_t) -> std::optional<lv::core::Error> {
        if (fail.load() && path == target && offset.get() >= std::uint64_t{1024} * 1024) {
            throw std::bad_alloc();
        }
        return std::nullopt;
    });
    Opened opened(target);
    REQUIRE(opened.ready());
    CHECK(opened.document.state() == Document::State::kError);
    CHECK(opened.document.errorMessage().contains(QStringLiteral("memory")));

    fail = false;
    opened.document.reload();
    REQUIRE(waitFor([&] { return opened.document.state() == Document::State::kReady; }));

    fail = true;
    REQUIRE_FALSE(opened.filter.apply({.text = "ERROR"}).has_value());
    REQUIRE(waitFor([&] { return opened.filter.state() == FilteredDocument::State::kError; }));
    CHECK(opened.filter.errorMessage().contains(QStringLiteral("memory")));
}
