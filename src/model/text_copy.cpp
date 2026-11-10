#include "model/text_copy.h"

#include "core/encoding/decode.h"
#include "core/util/assert.h"
#include "model/document.h"

#include <algorithm>
#include <string>
#include <vector>

namespace lv::model
{

namespace
{

constexpr std::uint64_t kChunkRows = 4096;

std::uint64_t lineSizeWithTerminator(const RowReader& reader, core::LineNumber line)
{
    const auto range = reader.content->index.rawLineRange(reader.snapshot, line);
    const bool complete = line.get() < reader.snapshot.completeLines.get();
    const std::uint64_t lineFeed = core::traits(reader.content->index.encoding()).codeUnitSize;
    return range.size() + (complete ? lineFeed : 0);
}

}  // namespace

std::uint64_t estimateCopyBytes(const RowReader& reader, const CopyRange& range)
{
    LV_ASSERT(reader.content != nullptr && range.firstRow <= range.lastRow);
    const auto& index = reader.content->index;
    if (reader.contiguous) {
        const auto first = index.rawLineRange(reader.snapshot, core::LineNumber(range.firstRow));
        const core::LineNumber lastLine(range.lastRow);
        const auto last = index.rawLineRange(reader.snapshot, lastLine);
        const std::uint64_t end = last.begin().get() + lineSizeWithTerminator(reader, lastLine);
        return end - first.begin().get();
    }
    std::uint64_t bytes = 0;
    for (std::uint64_t row = range.firstRow; row <= range.lastRow; ++row) {
        bytes += lineSizeWithTerminator(reader, reader.lineForRow(core::LineNumber(row)));
    }
    return bytes;
}

core::Result<QString> extractText(const RowReader& reader, const CopyRange& range,
                                  const std::stop_token& stop, const CopyProgress& progress)
{
    LV_ASSERT(reader.content != nullptr && range.firstRow <= range.lastRow);
    const auto& content = *reader.content;
    const core::Encoding encoding = content.index.encoding();
    QString out;
    std::u16string text;
    std::uint64_t processed = 0;
    std::vector<core::LineNumber> lines;

    for (std::uint64_t chunk = range.firstRow; chunk <= range.lastRow; chunk += kChunkRows) {
        if (stop.stop_requested()) {
            return core::makeError(core::ErrorCode::kCanceled, "copy canceled");
        }
        const std::uint64_t count = std::min(kChunkRows, range.lastRow - chunk + 1);
        lines.clear();
        for (std::uint64_t i = 0; i < count; ++i) {
            lines.push_back(reader.lineForRow(core::LineNumber(chunk + i)));
        }
        // Read runs of consecutive lines with one call each.
        std::size_t runStart = 0;
        while (runStart < lines.size()) {
            std::size_t runEnd = runStart + 1;
            while (runEnd < lines.size() && lines[runEnd].get() == lines[runEnd - 1].get() + 1) {
                ++runEnd;
            }
            const auto block = content.reader.readLines(reader.snapshot, lines[runStart],
                                                        core::LineCount(runEnd - runStart));
            if (!block) {
                return std::unexpected(block.error());
            }
            for (std::size_t i = 0; i < block->lines.size(); ++i) {
                const std::uint64_t row = chunk + runStart + i;
                const core::LineNumber line = lines[runStart + i];
                core::decodeLine(block->line(i), encoding, text);
                const auto length = static_cast<qsizetype>(text.size());
                qsizetype from = 0;
                qsizetype to = length;
                bool lineEnd = true;
                if (row == range.firstRow) {
                    from = std::clamp<qsizetype>(range.firstColumn, 0, length);
                }
                if (row == range.lastRow && range.lastColumn) {
                    to = std::clamp<qsizetype>(*range.lastColumn, 0, length);
                    lineEnd = false;
                }
                if (to > from) {
                    out.append(QStringView(text.data() + from, to - from));
                }
                if (lineEnd && line.get() < reader.snapshot.completeLines.get()) {
                    out.append(u'\n');
                }
                processed += lineSizeWithTerminator(reader, line);
            }
            runStart = runEnd;
        }
        if (progress) {
            progress(processed);
        }
    }
    return out;
}

}  // namespace lv::model
