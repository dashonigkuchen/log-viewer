#include "sequence_bench.h"

#include "core/index/sorted_u64_sequence.h"

#include <cstdint>
#include <random>
#include <vector>

namespace lv::bench
{

namespace
{

std::vector<std::uint64_t> lineEnds(std::size_t count)
{
    std::mt19937_64 random(1);
    std::vector<std::uint64_t> values(count);
    std::uint64_t offset = 0;
    for (auto& value : values) {
        offset += 110 + (random() % 41);  // like the "small" profile
        value = offset;
    }
    return values;
}

}  // namespace

void lineIndexAppend(benchmark::State& state)
{
    const auto values = lineEnds(1000000);
    for ([[maybe_unused]] auto iteration : state) {
        lv::core::SortedU64Sequence sequence;
        for (const auto value : values) {
            sequence.append(value);
        }
        sequence.publish();
        benchmark::DoNotOptimize(sequence.size());
        state.counters["bytes_per_value"] =
            static_cast<double>(sequence.memoryBytes()) / static_cast<double>(values.size());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(values.size()));
}

void lineIndexRandomAccess(benchmark::State& state)
{
    const auto values = lineEnds(1000000);
    lv::core::SortedU64Sequence sequence;
    for (const auto value : values) {
        sequence.append(value);
    }
    sequence.publish();
    std::mt19937_64 random(2);
    for ([[maybe_unused]] auto iteration : state) {
        benchmark::DoNotOptimize(sequence.at(random() % values.size()));
    }
    state.SetItemsProcessed(state.iterations());
}

void lineIndexLowerBound(benchmark::State& state)
{
    const auto values = lineEnds(1000000);
    lv::core::SortedU64Sequence sequence;
    for (const auto value : values) {
        sequence.append(value);
    }
    sequence.publish();
    std::mt19937_64 random(3);
    for ([[maybe_unused]] auto iteration : state) {
        benchmark::DoNotOptimize(sequence.lowerBound(random() % values.back()));
    }
    state.SetItemsProcessed(state.iterations());
}

}  // namespace lv::bench
