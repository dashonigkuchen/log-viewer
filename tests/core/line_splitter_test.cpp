#include "core/index/line_splitter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using lv::core::Encoding;
using lv::core::LineSplitter;

namespace
{

std::span<const std::byte> bytes(std::string_view text)
{
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

std::string utf16(std::string_view ascii, bool bigEndian)
{
    std::string out;
    for (const char c : ascii) {
        if (bigEndian) {
            out += '\0';
            out += c;
        } else {
            out += c;
            out += '\0';
        }
    }
    return out;
}

// Reference: line end offsets found by a byte-by-byte scan of the whole input.
std::vector<std::uint64_t> naiveSplit(std::string_view data, Encoding encoding, std::uint64_t start)
{
    std::vector<std::uint64_t> ends;
    if (encoding == Encoding::kUtf16Le || encoding == Encoding::kUtf16Be) {
        const bool be = encoding == Encoding::kUtf16Be;
        for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
            const char a = data[i];
            const char b = data[i + 1];
            if ((!be && a == '\n' && b == '\0') || (be && a == '\0' && b == '\n')) {
                ends.push_back(start + i + 2);
            }
        }
    } else {
        for (std::size_t i = 0; i < data.size(); ++i) {
            if (data[i] == '\n') {
                ends.push_back(start + i + 1);
            }
        }
    }
    return ends;
}

std::vector<std::uint64_t> split(std::string_view data, Encoding encoding, std::uint64_t start,
                                 const std::vector<std::size_t>& chunkSizes)
{
    LineSplitter splitter(encoding, start);
    std::vector<std::uint64_t> ends;
    std::size_t offset = 0;
    std::size_t chunk = 0;
    while (offset < data.size()) {
        const std::size_t size =
            std::min(chunkSizes.empty() ? data.size() : chunkSizes[chunk++ % chunkSizes.size()],
                     data.size() - offset);
        splitter.feed(bytes(data.substr(offset, size)), ends);
        offset += size;
    }
    REQUIRE(splitter.position() == start + data.size());
    return ends;
}

std::vector<std::uint64_t> split(std::string_view data, Encoding encoding = Encoding::kUtf8)
{
    return split(data, encoding, 0, {});
}

using Ends = std::vector<std::uint64_t>;

}  // namespace

TEST_CASE("single-byte line terminators (F-FILE-5)", "[core][index]")
{
    CHECK(split("").empty());
    CHECK(split("no newline").empty());
    CHECK(split("a\n") == Ends{2});
    CHECK(split("a\nb") == Ends{2});
    CHECK(split("a\nb\n") == Ends{2, 4});
    CHECK(split("a\r\nb\r\n") == Ends{3, 6});
    CHECK(split("a\rb\rc").empty());  // lone CR is not a line break
    CHECK(split("\n\n\n") == Ends{1, 2, 3});
    CHECK(split("\r\n\r\n") == Ends{2, 4});
    CHECK(split("x\r\r\n") == Ends{4});
    CHECK(split(std::string_view("a\0b\n", 4)) == Ends{4});
    for (const Encoding e : {Encoding::kWindows1252, Encoding::kIso88591}) {
        CHECK(split("Gr\xF6\xDF"
                    "e\nx\n",
                    e) == Ends{6, 8});
    }
}

TEST_CASE("UTF-16 line terminators", "[core][index]")
{
    const bool be = GENERATE(false, true);
    const Encoding encoding = be ? Encoding::kUtf16Be : Encoding::kUtf16Le;
    CHECK(split(utf16("a\nb\n", be), encoding) == Ends{4, 8});
    CHECK(split(utf16("a\r\nb", be), encoding) == Ends{6});
    CHECK(split(utf16("a\rb", be), encoding).empty());
    CHECK(split(utf16("", be), encoding).empty());
    // A 0x0A byte that is not a line feed code unit, e.g. U+0A41 or U+410A.
    const std::string tricky =
        be ? std::string("\x0A\x41\x41\x0A", 4) : std::string("\x41\x0A\x0A\x41", 4);
    CHECK(split(tricky, encoding).empty());
    // Misaligned "0A 00" across two code units must not count.
    const std::string misaligned =
        be ? std::string("\x41\x00\x0A\x41", 4) : std::string("\x41\x0A\x00\x41", 4);
    CHECK(split(misaligned, encoding).empty());
}

TEST_CASE("a start offset after a byte order mark", "[core][index]")
{
    const std::string le = utf16("ab\ncd\n", false);
    CHECK(split(le, Encoding::kUtf16Le, 2, {}) == Ends{8, 14});
    CHECK(split("x\ny\n", Encoding::kUtf8, 3, {}) == Ends{5, 7});
}

TEST_CASE("any buffer split gives the same result as the reference", "[core][index]")
{
    const auto encoding = GENERATE(Encoding::kUtf8, Encoding::kUtf16Le, Encoding::kUtf16Be);
    const auto seed = GENERATE(std::uint64_t{1}, std::uint64_t{2}, std::uint64_t{3});
    std::mt19937_64 random(seed);
    // Random content with many line feeds, carriage returns, zeros and 0x0A in odd positions.
    std::string data((std::size_t{64} * 1024) + (seed * 7), '\0');
    for (auto& c : data) {
        const auto roll = random() % 16;
        if (roll < 3) {
            c = '\n';
        } else if (roll < 5) {
            c = '\r';
        } else if (roll < 8) {
            c = '\0';
        } else {
            c = static_cast<char>(random() % 256);
        }
    }
    if (encoding != Encoding::kUtf8 && data.size() % 2 == 1) {
        data.pop_back();
    }
    const auto expected = naiveSplit(data, encoding, 0);
    REQUIRE_FALSE(expected.empty());
    const std::vector<std::vector<std::size_t>> schedules{
        {1}, {2}, {3}, {7, 1, 2}, {4096}, {1 << 20}, {5, 4096, 1, 17}};
    for (const auto& schedule : schedules) {
        REQUIRE(split(data, encoding, 0, schedule) == expected);
    }
    std::vector<std::size_t> randomSizes;
    randomSizes.reserve(200);
    for (int i = 0; i < 200; ++i) {
        randomSizes.push_back(1 + (random() % 3000));
    }
    REQUIRE(split(data, encoding, 0, randomSizes) == expected);
}

TEST_CASE("dense line feeds (more than eight per 64 bytes)", "[core][index]")
{
    // Exercises the SIMD path with many matches per step, and every alignment of the tail.
    std::mt19937_64 random(11);
    for (int round = 0; round < 200; ++round) {
        std::string data(static_cast<std::size_t>(random() % 700), 'x');
        const auto density = 1 + (random() % 4);
        for (auto& c : data) {
            if (random() % density == 0) {
                c = '\n';
            }
        }
        const auto start = static_cast<std::uint64_t>(random() % 1000);
        LineSplitter splitter(Encoding::kUtf8, start);
        std::vector<std::uint64_t> ends;
        splitter.feed(std::as_bytes(std::span<const char>(data.data(), data.size())), ends);
        std::vector<std::uint64_t> expected;
        for (std::size_t i = 0; i < data.size(); ++i) {
            if (data[i] == '\n') {
                expected.push_back(start + i + 1);
            }
        }
        REQUIRE(ends == expected);
    }
}
