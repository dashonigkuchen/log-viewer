#pragma once

#include <benchmark/benchmark.h>

namespace lv::bench
{

// SortedU64Sequence (line index storage) with 1M line ends of 110-150 bytes.
void lineIndexAppend(benchmark::State& state);
void lineIndexRandomAccess(benchmark::State& state);
void lineIndexLowerBound(benchmark::State& state);

}  // namespace lv::bench
