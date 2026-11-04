#include "decode_bench.h"

#include "core/encoding/decode.h"

#include <cstdint>
#include <span>
#include <string>

namespace lv::bench
{

void decodeLines(benchmark::State& state, lv::core::Encoding encoding)
{
    // One screen of typical lines (60 lines of 130 bytes) with some non-ASCII characters.
    std::string line = "2026-01-01 00:00:00.123 INFO  [worker-1] com.example.Service - ";
    const bool utf8 = encoding == lv::core::Encoding::kUtf8;
    while (line.size() < 130) {
        line += utf8 ? "Gr\xC3\xB6\xC3\x9F"
                       "e ok "
                     : "Gr\xF6\xDF"
                       "e ok ";
    }
    const auto raw = std::as_bytes(std::span<const char>(line.data(), line.size()));
    std::u16string text;
    std::vector<std::uint32_t> offsets;
    for ([[maybe_unused]] auto iteration : state) {
        for (int i = 0; i < 60; ++i) {
            lv::core::decodeLine(raw, encoding, text, &offsets, {.visibleControls = true});
            benchmark::DoNotOptimize(text.data());
        }
    }
    state.SetBytesProcessed(state.iterations() * 60 * static_cast<std::int64_t>(line.size()));
}

}  // namespace lv::bench
