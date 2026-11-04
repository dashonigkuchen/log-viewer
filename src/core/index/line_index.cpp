#include "core/index/line_index.h"

#include "core/util/assert.h"

#include <algorithm>
#include <array>

namespace lv::core
{

LineIndex::LineIndex(Encoding encoding, std::uint64_t dataStart)
{
    reset(encoding, dataStart);
}

void LineIndex::reset(Encoding encoding, std::uint64_t dataStart)
{
    m_encoding = encoding;
    m_dataStart = ByteOffset(dataStart);
    m_lineFeedSize = traits(encoding).codeUnitSize;
    m_ends.clear();
    m_lastAppendedEnd = dataStart;
    m_maxLineLength.store(0, std::memory_order_release);
    const std::scoped_lock lock(m_snapshotMutex);
    m_snapshot = Snapshot{.completeLines = LineCount(0),
                          .indexedEnd = m_dataStart,
                          .lastLineEnd = m_dataStart,
                          .generation = m_snapshot.generation + 1};
}

void LineIndex::append(std::span<const std::uint64_t> lineEnds)
{
    if (lineEnds.empty()) {
        return;
    }
    LV_ASSERT(lineEnds.front() >= m_lastAppendedEnd + m_lineFeedSize);
    // Longest distance between consecutive ends (a line length plus its line feed).
    std::uint64_t maxDistance = lineEnds.front() - m_lastAppendedEnd;
    for (std::size_t i = 1; i < lineEnds.size(); ++i) {
        LV_ASSERT(lineEnds[i] >= lineEnds[i - 1] + m_lineFeedSize);
        maxDistance = std::max(maxDistance, lineEnds[i] - lineEnds[i - 1]);
    }
    m_ends.append(lineEnds);
    m_lastAppendedEnd = lineEnds.back();
    const std::uint64_t maxLength =
        std::max(m_maxLineLength.load(std::memory_order_relaxed), maxDistance - m_lineFeedSize);
    m_maxLineLength.store(maxLength, std::memory_order_release);
}

void LineIndex::publish(std::uint64_t indexedEnd)
{
    LV_ASSERT(indexedEnd >= m_lastAppendedEnd);
    m_ends.publish();
    const std::scoped_lock lock(m_snapshotMutex);
    m_snapshot.completeLines = LineCount(m_ends.appendedCount());
    m_snapshot.indexedEnd = ByteOffset(indexedEnd);
    m_snapshot.lastLineEnd = ByteOffset(m_lastAppendedEnd);
}

LineIndex::Snapshot LineIndex::snapshot() const
{
    const std::scoped_lock lock(m_snapshotMutex);
    return m_snapshot;
}

ByteRange LineIndex::rawLineRange(const Snapshot& snapshot, LineNumber line) const
{
    LV_ASSERT(line.get() < snapshot.lineCount().get());
    const std::uint64_t n = line.get();
    const std::uint64_t complete = snapshot.completeLines.get();
    if (n == complete) {  // pending tail
        return {snapshot.lastLineEnd, snapshot.indexedEnd};
    }
    std::array<std::uint64_t, 2> ends{};
    std::uint64_t begin = m_dataStart.get();
    std::uint64_t end = 0;
    if (n == 0) {
        end = m_ends.at(0);
    } else {
        m_ends.copy(static_cast<std::size_t>(n - 1), ends);
        begin = ends[0];
        end = ends[1];
    }
    return {ByteOffset(begin), ByteOffset(end - m_lineFeedSize)};
}

std::optional<LineNumber> LineIndex::lineContaining(const Snapshot& snapshot,
                                                    ByteOffset offset) const
{
    if (offset < m_dataStart || offset >= snapshot.indexedEnd) {
        return std::nullopt;
    }
    const auto complete = static_cast<std::size_t>(snapshot.completeLines.get());
    // The first line end greater than the offset terminates the line that contains it.
    const std::size_t index = std::min(m_ends.lowerBound(offset.get() + 1), complete);
    return LineNumber(index);
}

}  // namespace lv::core
