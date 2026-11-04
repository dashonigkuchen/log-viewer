#pragma once

#include "loggen/loggen.h"

#include <benchmark/benchmark.h>

#include <cstddef>

namespace lv::bench
{

// Buffer size of the reference reader; the indexer uses the same size (ARCHITECTURE §3.4).
inline constexpr std::size_t kReadBufferSize = std::size_t{4} * 1024 * 1024;

// Reference for P-IDX-2: reads the whole file sequentially with 4 MiB buffers and does nothing
// else. The indexer must reach at least 90% of this throughput.
void readOnlySequential(benchmark::State& state, lv::loggen::Profile profile);

}  // namespace lv::bench
