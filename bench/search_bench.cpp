#include "search_bench.h"

#include "bench_data.h"
#include "core/concurrency/thread_pool.h"
#include "core/index/indexer.h"
#include "core/index/line_index.h"
#include "core/index/sorted_u64_sequence.h"
#include "core/platform/file.h"
#include "core/search/query.h"
#include "core/search/search_engine.h"

#include <cstdlib>
#include <span>
#include <stop_token>
#include <string>

namespace lv::bench
{

namespace
{

class CountingSink final : public lv::core::ResultSink
{
public:
    void append(std::span<const std::uint64_t> lines,
                const lv::core::SearchProgress& /*progress*/) override
    {
        count += lines.size();
    }

    std::uint64_t count = 0;
};

}  // namespace

void searchFile(benchmark::State& state, lv::loggen::Profile profile, std::string query, int mode,
                std::size_t threads)
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
    const bool utf16 = profile == lv::loggen::Profile::kUtf16;
    lv::core::LineIndex index(utf16 ? lv::core::Encoding::kUtf16Le : lv::core::Encoding::kUtf8,
                              utf16 ? 2U : 0U);
    if (!lv::core::Indexer::run(*file, index, std::stop_token{})) {
        state.SkipWithError("indexing failed");
        return;
    }
    lv::core::Query q;
    q.text = std::move(query);
    q.mode = static_cast<lv::core::QueryMode>(mode);
    const auto compiled = lv::core::CompiledQuery::compile(q);
    if (!compiled) {
        state.SkipWithError(compiled.error().message);
        return;
    }
    lv::core::ThreadPool pool(threads);
    lv::core::SearchRequest request;
    request.file = &*file;
    request.index = &index;
    request.snapshot = index.snapshot();
    request.matcher = compiled->get();
    // Chunk size experiments: LV_BENCH_CHUNK_KIB (default: engine default).
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read before the benchmark starts threads
    if (const char* chunk = std::getenv("LV_BENCH_CHUNK_KIB"); chunk != nullptr) {
        request.options.chunkBytes = std::stoul(chunk) * 1024;
    }
    std::uint64_t matches = 0;
    for ([[maybe_unused]] auto iteration : state) {
        CountingSink sink;
        if (!lv::core::SearchEngine::run(request, pool, std::stop_token{}, sink)) {
            state.SkipWithError("search failed");
            break;
        }
        matches = sink.count;
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(data.sizeBytes));
    state.counters["matches"] = static_cast<double>(matches);
    state.counters["threads"] = static_cast<double>(pool.size());
    // P-SRCH-5: memory of the match list (filled once outside the timed loop).
    lv::core::SortedU64Sequence list;
    lv::core::SequenceSink listSink(list);
    if (lv::core::SearchEngine::run(request, pool, std::stop_token{}, listSink) && !list.empty()) {
        state.counters["list_bytes_per_match"] =
            static_cast<double>(list.memoryBytes()) / static_cast<double>(list.size());
    }
}

}  // namespace lv::bench
