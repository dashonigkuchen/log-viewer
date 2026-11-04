#pragma once

#include "core/encoding/encoding.h"

#include <benchmark/benchmark.h>

namespace lv::bench
{

// Decoding one screen of lines with offset mapping (display path).
void decodeLines(benchmark::State& state, lv::core::Encoding encoding);

}  // namespace lv::bench
