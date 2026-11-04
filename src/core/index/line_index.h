#pragma once

#include "core/encoding/encoding.h"
#include "core/index/sorted_u64_sequence.h"
#include "core/util/strong_types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>

namespace lv::core
{

// The line index of one document (ARCHITECTURE §3.3): where every line starts and ends in the
// file. Stores the end offset of every terminated line (just after its line feed) in a
// SortedU64Sequence, about 1.2 bytes per line.
//
// The unterminated last line ("pending tail", SPEC F-TAIL-8) is not stored: it is everything
// between the last line end and the indexed end of the file. When the writer later finds its
// line feed, the line becomes a normal line, so it is never counted twice.
//
// Readers work on a Snapshot, a consistent view of the published state. Line numbers and
// offsets are only valid together with the snapshot they came from.
//
// Thread-safety: one writer (append, publish, reset); any number of readers (snapshot and the
// functions taking a snapshot) concurrently. reset() must not run concurrently with readers.
class LineIndex
{
public:
    struct Snapshot
    {
        // Lines terminated by a line feed.
        LineCount completeLines;
        // File offset up to which data was indexed.
        ByteOffset indexedEnd;
        // End of the last complete line (data start if there is none).
        ByteOffset lastLineEnd;
        std::uint64_t generation = 0;

        [[nodiscard]] bool hasPendingTail() const noexcept { return indexedEnd > lastLineEnd; }

        // Complete lines plus the pending tail, if any.
        [[nodiscard]] LineCount lineCount() const noexcept
        {
            return LineCount(completeLines.get() + (hasPendingTail() ? 1U : 0U));
        }

        bool operator==(const Snapshot&) const = default;
    };

    // `dataStart` is the first byte after the byte order mark (0 if there is none).
    LineIndex(Encoding encoding, std::uint64_t dataStart);
    LineIndex(const LineIndex&) = delete;
    LineIndex& operator=(const LineIndex&) = delete;
    LineIndex(LineIndex&&) = delete;
    LineIndex& operator=(LineIndex&&) = delete;
    ~LineIndex() = default;

    [[nodiscard]] Encoding encoding() const noexcept { return m_encoding; }

    [[nodiscard]] ByteOffset dataStart() const noexcept { return m_dataStart; }

    // Writer: appends line end offsets in increasing order (each > the previous one and > data
    // start). Not visible to readers before publish().
    void append(std::span<const std::uint64_t> lineEnds);

    // Writer: publishes all appended lines and the indexed end of the file (>= last line end).
    void publish(std::uint64_t indexedEnd);

    // Writer: drops everything and starts a new generation (reload, encoding change, truncation).
    // No reader may access the index concurrently.
    void reset(Encoding encoding, std::uint64_t dataStart);

    // Writer: end offset of the last appended line, or data start.
    [[nodiscard]] std::uint64_t appendedEnd() const noexcept { return m_lastAppendedEnd; }

    [[nodiscard]] Snapshot snapshot() const;

    // Raw byte range of a line: from its first byte up to, but excluding, the line feed code
    // unit. A carriage return before the line feed is included; LineReader removes it.
    // Requires line < snapshot.lineCount().
    [[nodiscard]] ByteRange rawLineRange(const Snapshot& snapshot, LineNumber line) const;

    // The line containing `offset`, or nullopt if the offset is before the data start or at or
    // after the indexed end. A line feed belongs to the line it terminates.
    [[nodiscard]] std::optional<LineNumber> lineContaining(const Snapshot& snapshot,
                                                           ByteOffset offset) const;

    // Longest raw line length in bytes seen so far (complete lines only; including a CR).
    [[nodiscard]] std::uint64_t maxLineLength() const noexcept
    {
        return m_maxLineLength.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t memoryBytes() const noexcept { return m_ends.memoryBytes(); }

private:
    Encoding m_encoding = Encoding::kUtf8;
    ByteOffset m_dataStart;
    std::uint64_t m_lineFeedSize = 1;
    SortedU64Sequence m_ends;

    // Writer-only.
    std::uint64_t m_lastAppendedEnd = 0;

    std::atomic<std::uint64_t> m_maxLineLength{0};
    mutable std::mutex m_snapshotMutex;
    Snapshot m_snapshot;
};

}  // namespace lv::core
