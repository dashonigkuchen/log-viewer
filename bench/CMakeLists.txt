# Benchmark suite (docs/PERFORMANCE.md). Built only with LV_BUILD_BENCH (release presets).
find_package(benchmark CONFIG REQUIRED)

add_executable(lv_bench main.cpp bench_data.cpp bench_data.h index_bench.cpp index_bench.h read_bench.cpp read_bench.h sequence_bench.cpp
                        sequence_bench.h)

target_link_libraries(lv_bench PRIVATE lv::loggen lv::core benchmark::benchmark lv::compile_options)
target_compile_definitions(
    lv_bench PRIVATE LV_BENCH_DEFAULT_DATA_DIR="${PROJECT_BINARY_DIR}/bench-data"
                     # std::getenv is deprecated by MSVC's secure CRT; the read is single-threaded.
                     $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

if(LV_BUILD_TESTS)
    # Smoke run: generates the small profile once (cached) and runs each benchmark briefly.
    add_test(NAME bench.smoke COMMAND lv_bench --benchmark_min_time=0.05s)
    set_tests_properties(bench.smoke PROPERTIES LABELS bench TIMEOUT 300)
endif()
