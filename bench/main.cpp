#include "bench_data.h"
#include "decode_bench.h"
#include "index_bench.h"
#include "read_bench.h"
#include "search_bench.h"
#include "sequence_bench.h"

#include <benchmark/benchmark.h>

#include <iostream>
#include <string>
#include <vector>

// Usage: lv_bench [Google Benchmark flags], e.g.
//   lv_bench --benchmark_filter=ReadOnly --benchmark_out=result.json --benchmark_out_format=json
// Data: $LV_BENCH_DATA (default <build>/bench-data), profiles: $LV_BENCH_PROFILES (default small).
int main(int argc, char** argv)
{
    std::vector<std::string> errors;
    const auto profiles = lv::bench::selectedProfiles(errors);
    for (const auto& error : errors) {
        std::cerr << "lv_bench: " << error << '\n';
    }
    if (!errors.empty()) {
        return 2;
    }

    for (const auto profile : profiles) {
        const std::string suffix = "/" + std::string(lv::loggen::profileName(profile));
        benchmark::RegisterBenchmark("BM_ReadOnly_Sequential" + suffix,
                                     lv::bench::readOnlySequential, profile)
            ->Unit(benchmark::kMillisecond)
            ->UseRealTime();
        benchmark::RegisterBenchmark("BM_Index" + suffix, lv::bench::indexFile, profile)
            ->Unit(benchmark::kMillisecond)
            ->UseRealTime();
        // P-SRCH-1..4: literal, regex, boolean with all cores; literal with one thread.
        benchmark::RegisterBenchmark("BM_Search_Literal" + suffix, lv::bench::searchFile, profile,
                                     std::string("ERROR"), 0, std::size_t{0})
            ->Unit(benchmark::kMillisecond)
            ->UseRealTime();
        benchmark::RegisterBenchmark("BM_Search_Literal_1T" + suffix, lv::bench::searchFile,
                                     profile, std::string("ERROR"), 0, std::size_t{1})
            ->Unit(benchmark::kMillisecond)
            ->UseRealTime();
        benchmark::RegisterBenchmark("BM_Search_Regex" + suffix, lv::bench::searchFile, profile,
                                     std::string("(ERROR|WARN).*timeout"), 1, std::size_t{0})
            ->Unit(benchmark::kMillisecond)
            ->UseRealTime();
        benchmark::RegisterBenchmark("BM_Search_Boolean" + suffix, lv::bench::searchFile, profile,
                                     std::string(R"(/ERROR/ AND NOT "heartbeat")"), 2,
                                     std::size_t{0})
            ->Unit(benchmark::kMillisecond)
            ->UseRealTime();
    }

    benchmark::RegisterBenchmark("BM_LineIndex_Append", lv::bench::lineIndexAppend)
        ->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark("BM_LineIndex_RandomAccess", lv::bench::lineIndexRandomAccess);
    benchmark::RegisterBenchmark("BM_LineIndex_LowerBound", lv::bench::lineIndexLowerBound);

    benchmark::RegisterBenchmark("BM_Decode_Utf8", lv::bench::decodeLines,
                                 lv::core::Encoding::kUtf8);
    benchmark::RegisterBenchmark("BM_Decode_Windows1252", lv::bench::decodeLines,
                                 lv::core::Encoding::kWindows1252);

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    std::cout << "lv_bench data directory: " << lv::bench::dataDirectory().string() << '\n';
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
