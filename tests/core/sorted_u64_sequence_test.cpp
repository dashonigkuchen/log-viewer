#include "core/index/sorted_u64_sequence.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <random>
#include <thread>
#include <vector>

using lv::core::SortedU64Sequence;

namespace
{

enum class Shape : std::uint8_t
{
    kSmallDeltas,
    kHugeDeltas,
    kMixed,
    kEqual,
};

std::vector<std::uint64_t> makeValues(std::size_t count, Shape shape, std::uint64_t seed)
{
    std::mt19937_64 random(seed);
    std::vector<std::uint64_t> values;
    values.reserve(count);
    std::uint64_t value = random() % 1000;
    for (std::size_t i = 0; i < count; ++i) {
        switch (shape) {
        case Shape::kSmallDeltas:
            value += 20 + (random() % 220);
            break;
        case Shape::kHugeDeltas:
            value += random() % (std::uint64_t{1} << 40U);
            break;
        case Shape::kMixed:
            value += (random() % 50 == 0) ? random() % (std::uint64_t{1} << 33U) : random() % 300;
            break;
        case Shape::kEqual:
            value += (random() % 10 == 0) ? 1U : 0U;
            break;
        }
        values.push_back(value);
    }
    return values;
}

void fill(SortedU64Sequence& sequence, const std::vector<std::uint64_t>& values)
{
    for (const auto value : values) {
        sequence.append(value);
    }
    sequence.publish();
}

void checkEqual(const SortedU64Sequence& sequence, const std::vector<std::uint64_t>& values)
{
    REQUIRE(sequence.size() == values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        REQUIRE(sequence.at(i) == values[i]);
    }
    std::vector<std::uint64_t> copied(values.size());
    sequence.copy(0, copied);
    REQUIRE(copied == values);
    if (values.size() > 10) {
        std::vector<std::uint64_t> middle(values.size() - 7);
        sequence.copy(3, middle);
        REQUIRE(std::equal(middle.begin(), middle.end(), values.begin() + 3));
    }
}

std::size_t referenceLowerBound(const std::vector<std::uint64_t>& values, std::uint64_t value)
{
    return static_cast<std::size_t>(std::ranges::lower_bound(values, value) - values.begin());
}

}  // namespace

TEST_CASE("empty sequence", "[core][index]")
{
    const SortedU64Sequence sequence;
    CHECK(sequence.empty());
    CHECK(sequence.lowerBound(0) == 0);
    CHECK(sequence.lowerBound(12345) == 0);
}

TEST_CASE("sequence matches a vector at block boundaries", "[core][index]")
{
    const auto count =
        GENERATE(std::size_t{1}, std::size_t{2}, std::size_t{127}, std::size_t{128},
                 std::size_t{129}, std::size_t{255}, std::size_t{256}, std::size_t{1000});
    const auto shape =
        GENERATE(Shape::kSmallDeltas, Shape::kHugeDeltas, Shape::kMixed, Shape::kEqual);
    const auto values = makeValues(count, shape, count * 31);
    SortedU64Sequence sequence;
    fill(sequence, values);
    checkEqual(sequence, values);
}

TEST_CASE("large random sequences round-trip", "[core][index]")
{
    const auto seed = GENERATE(std::uint64_t{17}, std::uint64_t{4242}, std::uint64_t{99991},
                               std::uint64_t{123457});
    const auto shape = GENERATE(Shape::kSmallDeltas, Shape::kHugeDeltas, Shape::kMixed);
    const auto values = makeValues(50000 + (seed % 300), shape, seed);
    SortedU64Sequence sequence;
    fill(sequence, values);
    checkEqual(sequence, values);
}

TEST_CASE("extreme values", "[core][index]")
{
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::uint64_t> values{0, 0, 1, kMax / 2, kMax - 1, kMax, kMax};
    while (values.size() < 300) {
        values.push_back(kMax);
    }
    std::vector<std::uint64_t> withJump(200, 0);
    withJump.insert(withJump.end(), 100, kMax);  // delta of 2^64 - 1 inside one block
    for (const auto& input : {values, withJump}) {
        SortedU64Sequence sequence;
        fill(sequence, input);
        checkEqual(sequence, input);
        CHECK(sequence.lowerBound(kMax) == referenceLowerBound(input, kMax));
    }
}

TEST_CASE("lowerBound matches std::lower_bound", "[core][index]")
{
    const auto shape = GENERATE(Shape::kSmallDeltas, Shape::kMixed, Shape::kEqual);
    const auto count = GENERATE(std::size_t{5}, std::size_t{128}, std::size_t{3000});
    const auto values = makeValues(count, shape, 7);
    SortedU64Sequence sequence;
    fill(sequence, values);
    std::mt19937_64 random(3);
    std::vector<std::uint64_t> probes{0, values.front(), values.back(), values.back() + 1};
    for (int i = 0; i < 500; ++i) {
        const std::uint64_t base = values[random() % values.size()];
        probes.push_back(base == 0 ? 0 : base + (random() % 3) - 1);
    }
    for (const auto probe : probes) {
        REQUIRE(sequence.lowerBound(probe) == referenceLowerBound(values, probe));
    }
}

TEST_CASE("values become visible only after publish", "[core][index]")
{
    SortedU64Sequence sequence;
    for (std::uint64_t i = 0; i < 300; ++i) {
        sequence.append(i * 10);
    }
    CHECK(sequence.appendedCount() == 300);
    CHECK(sequence.empty());
    sequence.publish();
    CHECK(sequence.size() == 300);
    sequence.append(5000);
    CHECK(sequence.size() == 300);
    CHECK(sequence.lowerBound(4000) == 300);
    sequence.publish();
    CHECK(sequence.at(300) == 5000);
    CHECK(sequence.lowerBound(4000) == 300);
}

TEST_CASE("clear resets the sequence", "[core][index]")
{
    SortedU64Sequence sequence;
    fill(sequence, makeValues(1000, Shape::kMixed, 1));
    sequence.clear();
    CHECK(sequence.empty());
    CHECK(sequence.appendedCount() == 0);
    const auto values = makeValues(500, Shape::kSmallDeltas, 2);
    fill(sequence, values);
    checkEqual(sequence, values);
}

TEST_CASE("memory use for typical line lengths", "[core][index]")
{
    // Deltas below 256 (log lines shorter than 256 bytes).
    const auto values = makeValues(1000000, Shape::kSmallDeltas, 11);
    SortedU64Sequence sequence;
    fill(sequence, values);
    const double bytesPerValue =
        static_cast<double>(sequence.memoryBytes()) / static_cast<double>(values.size());
    INFO("bytes per value: " << bytesPerValue);
    CHECK(bytesPerValue <= 1.2);
}

TEST_CASE("concurrent readers see a consistent prefix while the writer appends", "[core][index]")
{
    constexpr std::size_t kCount = 2000000;
    const auto values = makeValues(kCount, Shape::kMixed, 99);
    SortedU64Sequence sequence;
    std::atomic<bool> done{false};
    std::atomic<std::size_t> checks{0};
    std::atomic<bool> mismatch{false};

    std::vector<std::thread> readers;
    readers.reserve(4);
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&, r] {
            std::mt19937_64 random(static_cast<std::uint64_t>(r));
            std::vector<std::uint64_t> window(64);
            while (!done.load(std::memory_order_acquire)) {
                const std::size_t size = sequence.size();
                if (size == 0) {
                    continue;
                }
                const std::size_t index = random() % size;
                if (sequence.at(index) != values[index]) {
                    mismatch = true;
                }
                if (index + window.size() <= size) {
                    sequence.copy(index, window);
                    if (!std::equal(window.begin(), window.end(),
                                    values.begin() + static_cast<std::ptrdiff_t>(index))) {
                        mismatch = true;
                    }
                }
                const std::uint64_t probe = values[index];
                const std::size_t found = sequence.lowerBound(probe);
                if (found > index || values[found] != probe) {
                    mismatch = true;
                }
                checks.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (std::size_t i = 0; i < kCount; ++i) {
        sequence.append(values[i]);
        if (i % 1000 == 999) {
            sequence.publish();
        }
    }
    sequence.publish();
    done = true;
    for (auto& reader : readers) {
        reader.join();
    }
    CHECK_FALSE(mismatch.load());
    CHECK(checks.load() > 0);
    checkEqual(sequence, values);
}
