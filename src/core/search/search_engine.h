#pragma once

#include "core/index/line_index.h"
#include "core/util/result.h"
#include "core/util/strong_types.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>

namespace lv::core
{

class File;
class Matcher;
class SortedU64Sequence;
class ThreadPool;

struct SearchProgress
{
    // Lines of the range searched so far; always a prefix of the range.
    LineCount searchedLines;
    LineCount totalLines;
    std::uint64_t matches = 0;
    // Lines the matcher could not evaluate (regex limits; F-ROB-3).
    std::uint64_t skippedLines = 0;
};

// Receives search results in line order. All calls come from the thread running
// SearchEngine::run, one chunk at a time.
class ResultSink
{
public:
    ResultSink() = default;
    virtual ~ResultSink() = default;
    ResultSink(const ResultSink&) = delete;
    ResultSink& operator=(const ResultSink&) = delete;
    ResultSink(ResultSink&&) = delete;
    ResultSink& operator=(ResultSink&&) = delete;

    // `lines`: matching line numbers of the next chunk, ascending (may be empty);
    // `progress`: totals after this chunk.
    virtual void append(std::span<const std::uint64_t> lines, const SearchProgress& progress) = 0;
};

// Appends results to a match list and publishes them after every chunk, so readers always see
// an ordered prefix. progress() may be called from any thread.
class SequenceSink final : public ResultSink
{
public:
    explicit SequenceSink(SortedU64Sequence& matches);

    void append(std::span<const std::uint64_t> lines, const SearchProgress& progress) override;

    [[nodiscard]] SearchProgress progress() const;

private:
    SortedU64Sequence* m_matches;
    mutable std::mutex m_mutex;
    SearchProgress m_progress;
};

// Lines [first, first + count) of a snapshot (F-FILT-11).
struct SearchRange
{
    LineNumber first;
    LineCount count;
};

struct SearchOptions
{
    // Target amount of text per chunk; chunks always contain whole lines.
    std::size_t chunkBytes = std::size_t{1024} * 1024;
    // Chunks read or searched at the same time; 0 = twice the pool size. Bounds memory.
    std::size_t maxChunksInFlight = 0;
};

struct SearchRequest
{
    const File* file = nullptr;
    const LineIndex* index = nullptr;
    LineIndex::Snapshot snapshot;
    // nullopt = all lines of the snapshot.
    std::optional<SearchRange> range;
    // Matches UTF-8 text (CompiledQuery); shared by all workers.
    const Matcher* matcher = nullptr;
    SearchOptions options;
};

struct SearchStats
{
    std::uint64_t matches = 0;
    std::uint64_t skippedLines = 0;
    LineCount searchedLines;
    std::uint64_t chunks = 0;
};

// Multi-threaded line search (ARCHITECTURE §3.6). The range is split into chunks of whole lines;
// pool workers read their chunk with their own positional reads, convert it to UTF-8 where
// needed (non-UTF-8 documents, invalid UTF-8), and match line by line. A reorder buffer hands
// chunk results to the sink in line order, so partial results are always a correct prefix.
class SearchEngine
{
public:
    // Blocks until the search is done. Errors: kCanceled when `stop` (or the pool) was stopped,
    // kOutOfRange when the file is shorter than or differs from the index, I/O errors. Results
    // already passed to the sink stay valid (an ordered prefix) in every case.
    [[nodiscard]] static Result<SearchStats> run(const SearchRequest& request, ThreadPool& pool,
                                                 const std::stop_token& stop, ResultSink& sink);
};

}  // namespace lv::core
