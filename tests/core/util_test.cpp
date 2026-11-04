#include "core/util/assert.h"
#include "core/util/result.h"
#include "core/util/scope_guard.h"
#include "core/util/strong_types.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

using lv::core::ByteOffset;
using lv::core::ByteRange;
using lv::core::LineCount;
using lv::core::LineNumber;

namespace
{

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

struct AssertionFired
{
    std::string expression;
};

[[noreturn]] void throwingHandler(std::string_view expression,
                                  const std::source_location& /*location*/)
{
    throw AssertionFired{std::string(expression)};
}

// Installs a handler that throws, so tests can observe failed assertions.
class ThrowingAsserts
{
public:
    ThrowingAsserts()
        : m_previous(lv::core::setAssertHandler(&throwingHandler))
    {}

    ThrowingAsserts(const ThrowingAsserts&) = delete;
    ThrowingAsserts& operator=(const ThrowingAsserts&) = delete;
    ThrowingAsserts(ThrowingAsserts&&) = delete;
    ThrowingAsserts& operator=(ThrowingAsserts&&) = delete;

    ~ThrowingAsserts() { lv::core::setAssertHandler(m_previous); }

private:
    lv::core::AssertHandler m_previous;
};

}  // namespace

TEST_CASE("strong types are distinct and explicit", "[core][util]")
{
    STATIC_CHECK_FALSE(std::is_convertible_v<std::uint64_t, LineNumber>);
    STATIC_CHECK_FALSE(std::is_convertible_v<LineNumber, LineCount>);
    STATIC_CHECK_FALSE(std::is_convertible_v<ByteOffset, LineNumber>);
    STATIC_CHECK_FALSE(std::is_convertible_v<LineNumber, std::uint64_t>);
    STATIC_CHECK(sizeof(LineNumber) == 8);
    STATIC_CHECK(sizeof(ByteRange) == 16);
    STATIC_CHECK(LineNumber().get() == 0);
}

TEST_CASE("line arithmetic", "[core][util]")
{
    constexpr LineNumber kTen(10);
    STATIC_CHECK((kTen + LineCount(5)).get() == 15);
    STATIC_CHECK((kTen - LineCount(10)).get() == 0);
    STATIC_CHECK((LineNumber(15) - kTen) == LineCount(5));
    STATIC_CHECK((LineCount(2) + LineCount(3)) == LineCount(5));
    STATIC_CHECK((LineCount(3) - LineCount(3)) == LineCount(0));
    STATIC_CHECK(kTen.displayNumber() == 11);
    LineNumber n(1);
    ++n;
    CHECK(n == LineNumber(2));
    --n;
    --n;
    CHECK(n == LineNumber(0));
    CHECK(LineNumber(kMax - 1) + LineCount(1) == LineNumber(kMax));
    CHECK(LineNumber(3) < LineNumber(4));
    CHECK(LineCount(4) >= LineCount(4));
}

TEST_CASE("byte arithmetic and ranges", "[core][util]")
{
    constexpr ByteOffset kOffset(100);
    STATIC_CHECK((kOffset + 28).get() == 128);
    STATIC_CHECK((kOffset - 100).get() == 0);
    STATIC_CHECK((ByteOffset(128) - kOffset) == 28);

    constexpr ByteRange kRange(ByteOffset(10), ByteOffset(20));
    STATIC_CHECK(kRange.size() == 10);
    STATIC_CHECK_FALSE(kRange.empty());
    STATIC_CHECK(kRange.contains(ByteOffset(10)));
    STATIC_CHECK(kRange.contains(ByteOffset(19)));
    STATIC_CHECK_FALSE(kRange.contains(ByteOffset(20)));
    STATIC_CHECK(ByteRange::fromSize(ByteOffset(10), 10) == kRange);
    STATIC_CHECK(ByteRange().empty());
    STATIC_CHECK(ByteRange(ByteOffset(5), ByteOffset(5)).empty());
}

TEST_CASE("formatting of strong types", "[core][util]")
{
    CHECK(lv::core::toString(LineNumber(41)) == "line#41");
    CHECK(lv::core::toString(LineCount(3)) == "3 lines");
    CHECK(lv::core::toString(ByteOffset(7)) == "@7");
    CHECK(lv::core::toString(ByteRange(ByteOffset(1), ByteOffset(4))) == "[1, 4)");
    std::ostringstream out;
    out << LineNumber(1) << ' ' << ByteOffset(kMax);
    CHECK(out.str() == "line#1 @18446744073709551615");
}

#if LV_ASSERTS_ENABLED
TEST_CASE("overflow and underflow trigger assertions", "[core][util][assert]")
{
    const ThrowingAsserts asserts;
    CHECK_THROWS_AS(LineNumber(0) - LineCount(1), AssertionFired);
    CHECK_THROWS_AS(LineNumber(kMax) + LineCount(1), AssertionFired);
    CHECK_THROWS_AS(LineNumber(1) - LineNumber(2), AssertionFired);
    CHECK_THROWS_AS(LineNumber(kMax).displayNumber(), AssertionFired);
    CHECK_THROWS_AS(LineCount(1) - LineCount(2), AssertionFired);
    CHECK_THROWS_AS(LineCount(kMax) + LineCount(1), AssertionFired);
    CHECK_THROWS_AS(ByteOffset(kMax) + 1, AssertionFired);
    CHECK_THROWS_AS(ByteOffset(0) - 1, AssertionFired);
    CHECK_THROWS_AS(ByteOffset(1) - ByteOffset(2), AssertionFired);
    CHECK_THROWS_AS(ByteRange(ByteOffset(2), ByteOffset(1)), AssertionFired);
    LineNumber n;
    CHECK_THROWS_AS(--n, AssertionFired);
}

TEST_CASE("assertion handler receives the expression", "[core][util][assert]")
{
    const ThrowingAsserts asserts;
    try {
        LV_ASSERT(1 + 1 == 3);
        FAIL("assertion did not fire");
    } catch (const AssertionFired& fired) {
        CHECK(fired.expression == "1 + 1 == 3");
    }
}
#endif

TEST_CASE("Error formatting", "[core][util]")
{
    const lv::core::Error error{
        .code = lv::core::ErrorCode::kIoError, .systemCode = 5, .message = "read failed"};
    CHECK(error.toString() == "IoError: read failed (system error 5)");
    CHECK(lv::core::Error{.code = lv::core::ErrorCode::kCanceled, .systemCode = 0, .message = {}}
              .toString() == "Canceled");
    CHECK(lv::core::toString(lv::core::ErrorCode::kNotFound) == "NotFound");
    CHECK(lv::core::toString(lv::core::ErrorCode::kOutOfMemory) == "OutOfMemory");
    std::ostringstream out;
    out << error;
    CHECK(out.str() == error.toString());
}

namespace
{

lv::core::Result<int> parsePositive(int value)
{
    if (value <= 0) {
        return lv::core::makeError(lv::core::ErrorCode::kInvalidArgument, "not positive");
    }
    return value;
}

}  // namespace

TEST_CASE("Result carries values and errors", "[core][util]")
{
    const auto ok = parsePositive(3);
    REQUIRE(ok.has_value());
    CHECK(*ok == 3);
    const auto bad = parsePositive(-1);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == lv::core::ErrorCode::kInvalidArgument);
    CHECK(bad.error().message == "not positive");
    const auto doubled = parsePositive(4).map([](int v) { return v * 2; });
    CHECK(doubled.value() == 8);
}

TEST_CASE("ScopeGuard runs unless dismissed", "[core][util]")
{
    int calls = 0;
    {
        const lv::core::ScopeGuard guard([&calls]() noexcept { ++calls; });
    }
    CHECK(calls == 1);
    {
        lv::core::ScopeGuard guard([&calls]() noexcept { ++calls; });
        guard.dismiss();
    }
    CHECK(calls == 1);
}
