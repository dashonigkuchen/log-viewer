#include "core/tail/change_detector.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using lv::core::Change;
using lv::core::classify;
using lv::core::FileSnapshot;
using lv::core::fingerprintOf;
using lv::core::Observation;
using lv::core::PathState;

namespace
{

const FileSnapshot kLast{.size = 100, .fingerprint = fingerprintOf("start")};

Observation same(std::uint64_t size)
{
    return {.size = size,
            .fingerprint = fingerprintOf("start"),
            .pathState = PathState::kExists,
            .sameFile = true};
}

}  // namespace

TEST_CASE("classify covers every change", "[core][tail]")
{
    CHECK(classify(kLast, same(100)) == Change::kUnchanged);
    CHECK(classify(kLast, same(150)) == Change::kGrown);
    CHECK(classify(kLast, same(99)) == Change::kTruncated);
    CHECK(classify(kLast, same(0)) == Change::kTruncated);

    auto rewritten = same(200);
    rewritten.fingerprint = fingerprintOf("other");
    CHECK(classify(kLast, rewritten) == Change::kTruncated);  // rewritten from the start

    auto replaced = same(10);
    replaced.sameFile = false;
    CHECK(classify(kLast, replaced) == Change::kReplaced);  // takes priority over truncated

    auto missing = replaced;
    missing.pathState = PathState::kMissing;
    CHECK(classify(kLast, missing) == Change::kMissing);  // takes priority over everything
}

TEST_CASE("an empty file grows into its first line", "[core][tail]")
{
    const FileSnapshot empty{.size = 0, .fingerprint = fingerprintOf("")};
    // The fingerprint covers the bytes known before (none), so new bytes do not change it.
    CHECK(classify(empty, {.size = 5, .fingerprint = fingerprintOf("")}) == Change::kGrown);
    CHECK(classify(empty, {.size = 0, .fingerprint = fingerprintOf("")}) == Change::kUnchanged);
}

TEST_CASE("fingerprints hash the given bytes", "[core][tail]")
{
    CHECK(fingerprintOf("abc") == fingerprintOf(std::string("abc")));
    CHECK(fingerprintOf("abc") != fingerprintOf("abd"));
    CHECK(fingerprintOf("abc").length == 3);
    CHECK(lv::core::toString(Change::kReplaced) == "replaced");
}
