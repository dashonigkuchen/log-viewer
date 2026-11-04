#pragma once

#include "core/index/line_index.h"
#include "core/platform/file.h"
#include "core/util/result.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stop_token>

namespace lv::core
{

struct IndexProgress
{
    std::uint64_t indexedBytes = 0;  // file offset reached
    std::uint64_t totalBytes = 0;    // file size when the run started
    LineCount lines;                 // lines in the index (incl. pending tail)
};

using IndexProgressCallback = std::function<void(const IndexProgress&)>;

struct IndexerOptions
{
    // Read block size; 4 MiB is tuned in M1-10.
    std::size_t blockSize = std::size_t{4} * 1024 * 1024;
    // Buffers in the read-ahead ring (>= 2).
    std::size_t bufferCount = 3;
    // Minimum time between progress callbacks (the final one is always sent).
    std::chrono::milliseconds progressInterval{50};
};

struct IndexStats
{
    std::uint64_t bytesRead = 0;
    LineCount linesAdded;
};

// Builds or extends a LineIndex from a file (ARCHITECTURE §3.4).
//
// Indexing starts at index.appendedEnd() (the data start for a new index, the end of the last
// complete line afterwards), so calling run() again after the file grew indexes only the new
// data (incremental mode, SPEC F-TAIL-1). It reads up to the file size seen at the start.
//
// A read-ahead thread reads blocks into a small ring of reusable buffers while the calling
// thread scans them; the index is published after every block, so readers always see a
// consistent prefix. Cancellation through `stop` is checked per block.
//
// Errors: kCanceled (stop requested; the published prefix stays valid), kOutOfRange (the file
// is smaller than the indexed data: it was truncated), I/O errors from File::readAt.
//
// Thread-safety: the caller is the single writer of `index` for the duration of the call.
class Indexer
{
public:
    [[nodiscard]] static Result<IndexStats> run(const File& file, LineIndex& index,
                                                const std::stop_token& stop,
                                                const IndexProgressCallback& progress = {},
                                                const IndexerOptions& options = {});
};

}  // namespace lv::core
