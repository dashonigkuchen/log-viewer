#include "index_bench.h"

#include "bench_data.h"
#include "core/index/indexer.h"
#include "core/index/line_index.h"
#include "core/platform/file.h"

#include <stop_token>

namespace lv::bench
{

void indexFile(benchmark::State& state, lv::loggen::Profile profile)
{
    const BenchFile data = ensureData(profile);
    if (!data.error.empty()) {
        state.SkipWithError(data.error);
        return;
    }
    const auto file = lv::core::File::open(data.path);
    if (!file) {
        state.SkipWithError(file.error().toString());
        return;
    }
    std::uint64_t lines = 0;
    std::size_t memory = 0;
    for ([[maybe_unused]] auto iteration : state) {
        // Encoding detection is not part of the measurement; the profiles are known.
        const bool utf16 = profile == lv::loggen::Profile::kUtf16;
        lv::core::LineIndex index(utf16 ? lv::core::Encoding::kUtf16Le : lv::core::Encoding::kUtf8,
                                  utf16 ? 2U : 0U);
        const auto result = lv::core::Indexer::run(*file, index, std::stop_token{});
        if (!result) {
            state.SkipWithError(result.error().toString());
            break;
        }
        lines = index.snapshot().lineCount().get();
        memory = index.memoryBytes();
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(data.sizeBytes));
    state.counters["lines"] = static_cast<double>(lines);
    state.counters["index_bytes_per_line"] =
        lines == 0 ? 0.0 : static_cast<double>(memory) / static_cast<double>(lines);
}

}  // namespace lv::bench
