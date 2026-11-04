#include "read_bench.h"

#include "bench_data.h"

#include <cstdint>
#include <fstream>
#include <ios>
#include <vector>

namespace lv::bench
{

void readOnlySequential(benchmark::State& state, lv::loggen::Profile profile)
{
    const BenchFile file = ensureData(profile);
    if (!file.error.empty()) {
        state.SkipWithError(file.error);
        return;
    }
    std::vector<char> buffer(kReadBufferSize);
    std::uint64_t total = 0;
    for ([[maybe_unused]] auto iteration : state) {
        std::ifstream in(file.path, std::ios::binary);
        if (!in) {
            state.SkipWithError("cannot open " + file.path.string());
            break;
        }
        std::uint64_t bytes = 0;
        while (true) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = in.gcount();
            if (count <= 0) {
                break;
            }
            bytes += static_cast<std::uint64_t>(count);
            benchmark::DoNotOptimize(buffer.data());
            benchmark::ClobberMemory();
        }
        if (bytes != file.sizeBytes) {
            state.SkipWithError("short read of " + file.path.string());
            break;
        }
        total += bytes;
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(total));
    state.counters["file_mib"] =
        static_cast<double>(file.sizeBytes) / static_cast<double>(lv::loggen::kMiB);
}

}  // namespace lv::bench
