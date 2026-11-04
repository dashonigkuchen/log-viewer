#pragma once

#include "loggen/loggen.h"

#include <benchmark/benchmark.h>

namespace lv::bench
{

// Full index of a generated profile (P-IDX-1, P-IDX-2, P-IDX-3).
void indexFile(benchmark::State& state, lv::loggen::Profile profile);

}  // namespace lv::bench
