#pragma once

#include "loggen/loggen.h"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <string>

namespace lv::bench
{

// Full search of one generated profile with `threads` workers (0 = default pool size).
void searchFile(benchmark::State& state, lv::loggen::Profile profile, std::string query, int mode,
                std::size_t threads);

}  // namespace lv::bench
