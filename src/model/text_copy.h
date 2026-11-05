#pragma once

#include "core/index/line_index.h"
#include "core/util/result.h"
#include "core/util/strong_types.h"

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

namespace lv::model
{

struct DocumentContent;

// Everything needed to read the rows of a LineSource off the UI thread (SPEC F-VIEW-8): the
// content of one generation, a snapshot of its index, and the row -> line mapping.
//
// Thread-safety: may be copied to and used from any thread; `lineForRow` must be thread-safe.
struct RowReader
{
    std::shared_ptr<const DocumentContent> content;
    core::LineIndex::Snapshot snapshot;
    std::function<core::LineNumber(core::LineNumber row)> lineForRow;
    // Rows are consecutive file lines (the full file); enables an O(1) size estimate.
    bool contiguous = false;
};

// The text to copy, in the coordinates of TextSelection: rows [firstRow, lastRow], starting at
// UTF-16 column `firstColumn` of the first row. `lastColumn` ends the text inside the last row;
// nullopt means "whole last row including its line feed" (line selections).
struct CopyRange
{
    std::uint64_t firstRow = 0;
    std::uint64_t lastRow = 0;
    qsizetype firstColumn = 0;
    std::optional<qsizetype> lastColumn;
};

// Raw bytes of the rows in `range` including line terminators. For UTF-8 and single-byte files
// without CRs this is the size of the copied text in bytes (within the partial first and last
// rows); UTF-16 files report twice the character count. O(1) for contiguous readers.
[[nodiscard]] std::uint64_t estimateCopyBytes(const RowReader& reader, const CopyRange& range);

// Called with the raw bytes processed so far (see estimateCopyBytes).
using CopyProgress = std::function<void(std::uint64_t processedBytes)>;

// Extracts the original text of `range` (rows joined with "\n", no line feed after a partial
// last line of the file). Checks `stop` every few thousand rows. Errors: kCanceled, read errors.
[[nodiscard]] core::Result<QString> extractText(const RowReader& reader, const CopyRange& range,
                                                const std::stop_token& stop,
                                                const CopyProgress& progress = {});

}  // namespace lv::model
