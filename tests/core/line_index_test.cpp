#include "core/index/line_index.h"
#include "core/index/line_splitter.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using lv::core::ByteOffset;
using lv::core::ByteRange;
using lv::core::Encoding;
using lv::core::LineIndex;
using lv::core::LineNumber;

namespace
{

// Feeds `text` (appended at the current end) through a splitter into the index and publishes.
class Fixture
{
public:
    explicit Fixture(Encoding encoding = Encoding::kUtf8, std::uint64_t dataStart = 0)
        : m_index(encoding, dataStart)
        , m_splitter(encoding, dataStart)
        , m_content(dataStart, 'B')
    {}

    void write(std::string_view text)
    {
        m_content += text;
        std::vector<std::uint64_t> ends;
        m_splitter.feed(std::as_bytes(std::span<const char>(text.data(), text.size())), ends);
        m_index.append(ends);
        m_index.publish(m_splitter.position());
    }

    [[nodiscard]] std::vector<std::string> lines() const
    {
        const auto snapshot = m_index.snapshot();
        std::vector<std::string> result;
        for (std::uint64_t i = 0; i < snapshot.lineCount().get(); ++i) {
            const ByteRange range = m_index.rawLineRange(snapshot, LineNumber(i));
            result.push_back(m_content.substr(range.begin().get(), range.size()));
        }
        return result;
    }

    LineIndex& index() { return m_index; }

    [[nodiscard]] const std::string& content() const { return m_content; }

private:
    LineIndex m_index;
    lv::core::LineSplitter m_splitter;
    std::string m_content;
};

using Lines = std::vector<std::string>;

}  // namespace

TEST_CASE("line counts for the F-FILE-5 cases", "[core][index]")
{
    {
        Fixture f;
        f.write("");
        CHECK(f.index().snapshot().lineCount().get() == 0);
        CHECK(f.lines().empty());
    }
    {
        Fixture f;
        f.write("a");
        const auto snapshot = f.index().snapshot();
        CHECK(snapshot.hasPendingTail());
        CHECK(snapshot.lineCount().get() == 1);
        CHECK(f.lines() == Lines{"a"});
    }
    {
        Fixture f;
        f.write("a\n");
        CHECK_FALSE(f.index().snapshot().hasPendingTail());
        CHECK(f.lines() == Lines{"a"});
    }
    {
        Fixture f;
        f.write("a\r\n\r\n");
        CHECK(f.lines() == Lines{"a\r", "\r"});  // raw ranges keep the CR; the reader strips it
    }
    {
        Fixture f;
        f.write("\n\nx");
        CHECK(f.lines() == Lines{"", "", "x"});
    }
}

TEST_CASE("the pending tail is completed exactly once", "[core][index]")
{
    Fixture f;
    f.write("first\nsec");
    CHECK(f.lines() == Lines{"first", "sec"});
    f.write("ond");
    CHECK(f.lines() == Lines{"first", "second"});
    f.write("\n");
    CHECK(f.lines() == Lines{"first", "second"});
    CHECK_FALSE(f.index().snapshot().hasPendingTail());
    f.write("third\nfourth\n");
    CHECK(f.lines() == Lines{"first", "second", "third", "fourth"});
}

TEST_CASE("lineContaining maps every byte to its line", "[core][index]")
{
    Fixture f;
    f.write("ab\r\n\nxyz\nlast");
    const auto snapshot = f.index().snapshot();
    const std::string& content = f.content();
    std::uint64_t line = 0;
    for (std::uint64_t offset = 0; offset < content.size(); ++offset) {
        INFO("offset " << offset);
        CHECK(f.index().lineContaining(snapshot, ByteOffset(offset)) == LineNumber(line));
        if (content[offset] == '\n') {
            ++line;
        }
    }
    CHECK_FALSE(f.index().lineContaining(snapshot, ByteOffset(content.size())).has_value());
}

TEST_CASE("data start after a BOM", "[core][index]")
{
    Fixture f(Encoding::kUtf8, 3);
    f.write("x\ny");
    CHECK(f.lines() == Lines{"x", "y"});
    const auto snapshot = f.index().snapshot();
    CHECK_FALSE(f.index().lineContaining(snapshot, ByteOffset(2)).has_value());
    CHECK(f.index().lineContaining(snapshot, ByteOffset(3)) == LineNumber(0));
}

TEST_CASE("UTF-16 ranges exclude the 2-byte line feed", "[core][index]")
{
    Fixture f(Encoding::kUtf16Le, 2);
    f.write(std::string("a\0\n\0b\0", 6));
    const auto snapshot = f.index().snapshot();
    REQUIRE(snapshot.lineCount().get() == 2);
    CHECK(f.index().rawLineRange(snapshot, LineNumber(0)) ==
          ByteRange(ByteOffset(2), ByteOffset(4)));
    CHECK(f.index().rawLineRange(snapshot, LineNumber(1)) ==
          ByteRange(ByteOffset(6), ByteOffset(8)));
}

TEST_CASE("max line length and reset", "[core][index]")
{
    Fixture f;
    f.write("12\n12345\r\n1\nvery long unterminated tail");
    CHECK(f.index().maxLineLength() == 6);
    const auto before = f.index().snapshot().generation;
    f.index().reset(Encoding::kWindows1252, 0);
    const auto after = f.index().snapshot();
    CHECK(after.generation == before + 1);
    CHECK(after.lineCount().get() == 0);
    CHECK(f.index().maxLineLength() == 0);
    CHECK(f.index().encoding() == Encoding::kWindows1252);
}

TEST_CASE("unpublished lines are invisible", "[core][index]")
{
    LineIndex index(Encoding::kUtf8, 0);
    const std::vector<std::uint64_t> ends{2, 4, 6};
    index.append(ends);
    CHECK(index.snapshot().lineCount().get() == 0);
    CHECK(index.appendedEnd() == 6);
    index.publish(7);
    const auto snapshot = index.snapshot();
    CHECK(snapshot.completeLines.get() == 3);
    CHECK(snapshot.lineCount().get() == 4);
}

TEST_CASE("offsets near 2^63 keep exact lines (F-FILE-6)", "[core][index]")
{
    // A synthetic index whose lines end near 2^63 bytes: no real file is needed because the
    // index only stores line end offsets.
    constexpr std::uint64_t kBase = std::uint64_t{1} << 62U;
    LineIndex index(Encoding::kUtf8, 0);
    std::vector<std::uint64_t> ends;
    for (std::uint64_t i = 1; i <= 1000; ++i) {
        ends.push_back(i * (kBase / 1000 * 2 - 7));  // the last end is just below 2^63
    }
    index.append(ends);
    index.publish(ends.back() + 1);
    const auto snapshot = index.snapshot();
    REQUIRE(snapshot.lineCount().get() == 1001);  // 1000 lines plus the pending tail
    for (const std::uint64_t line :
         {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{500}, std::uint64_t{999}}) {
        const ByteRange range = index.rawLineRange(snapshot, LineNumber(line));
        CHECK(range.end().get() == ends[line] - 1);  // excludes the line feed
        CHECK(index.lineContaining(snapshot, ByteOffset(ends[line] - 1)) == LineNumber(line));
        CHECK(index.lineContaining(snapshot, ByteOffset(ends[line])) == LineNumber(line + 1));
    }
    CHECK(ends.back() < (std::uint64_t{1} << 63U));
    CHECK(ends.back() > (std::uint64_t{1} << 63U) - (std::uint64_t{1} << 40U));
}
