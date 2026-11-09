#include "model/filtered_line_source.h"

#include "core/index/sorted_u64_sequence.h"
#include "core/util/assert.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "model/mark_set.h"

#include <algorithm>
#include <utility>

namespace lv::model
{
namespace
{

struct RowTarget
{
    std::uint64_t line = 0;
    bool extra = false;
};

// Row -> line for a layout; `matchAt(index)` gives the line of match `index`.
template <typename MatchAt>
RowTarget targetOf(std::uint64_t row, const std::vector<std::uint64_t>& extraLines,
                   const std::vector<std::uint64_t>& extraRows, MatchAt matchAt)
{
    const auto it = std::ranges::lower_bound(extraRows, row);
    const auto before = static_cast<std::size_t>(it - extraRows.begin());
    if (it != extraRows.end() && *it == row) {
        return {.line = extraLines[before], .extra = true};
    }
    return {.line = matchAt(row - before), .extra = false};
}

}  // namespace

FilteredLineSource::FilteredLineSource(const FilteredDocument& filter,
                                       const DocumentLineSource& lines, QObject* parent)
    : LineSource(parent)
    , m_filter(&filter)
    , m_lines(&lines)
{
    connect(&filter, &FilteredDocument::matchesAppended, this,
            &FilteredLineSource::onMatchesAppended);
    connect(&filter, &FilteredDocument::reset, this, &FilteredLineSource::onFilterReset);
    m_layout.matchCount = filter.matchCount();
}

void FilteredLineSource::setMarks(const MarkSet* marks)
{
    if (!m_marks.isNull()) {
        disconnect(m_marks, nullptr, this, nullptr);
    }
    m_marks = marks;
    if (marks != nullptr) {
        connect(marks, &MarkSet::changed, this, &FilteredLineSource::onMarksChanged);
    }
    onMarksChanged();
}

void FilteredLineSource::setShowMarks(bool show)
{
    if (show == m_showMarks) {
        return;
    }
    m_showMarks = show;
    onMarksChanged();
}

void FilteredLineSource::rebuild(std::uint64_t matchCount)
{
    m_layout.matchCount = matchCount;
    m_layout.extraLines.clear();
    m_layout.extraRows.clear();
    if (!m_showMarks || m_marks.isNull()) {
        return;
    }
    for (const core::LineNumber line : m_marks->lines()) {
        const std::uint64_t index = std::min(m_filter->lowerBound(line), matchCount);
        if (index < matchCount && m_filter->matchAt(index) == line) {
            continue;  // already a match
        }
        m_layout.extraRows.push_back(index + m_layout.extraLines.size());
        m_layout.extraLines.push_back(line.get());
    }
}

void FilteredLineSource::onMatchesAppended(quint64 matchCount)
{
    if (m_layout.extraLines.empty()) {
        // Without extra rows, new matches only append (marks that are matches stay matches).
        m_layout.matchCount = matchCount;
        emit linesAppended(lineCount().get());
        return;
    }
    // New matches may land before extra rows or turn them into matches.
    emit rowsAboutToChange();
    rebuild(matchCount);
    emit rowsChanged();
}

void FilteredLineSource::onFilterReset()
{
    rebuild(m_filter->matchCount());
    emit reset();
}

void FilteredLineSource::onMarksChanged()
{
    emit rowsAboutToChange();
    rebuild(m_layout.matchCount);
    emit rowsChanged();
}

core::LineCount FilteredLineSource::lineCount() const
{
    return core::LineCount(m_layout.matchCount + m_layout.extraLines.size());
}

core::LineNumber FilteredLineSource::originalLineNumber(core::LineNumber row) const
{
    return core::LineNumber(
        targetOf(row.get(), m_layout.extraLines, m_layout.extraRows, [this](std::uint64_t index) {
            return m_filter->matchAt(index).get();
        }).line);
}

bool FilteredLineSource::isExtraRow(core::LineNumber row) const
{
    return std::ranges::binary_search(m_layout.extraRows, row.get());
}

std::uint64_t FilteredLineSource::rowsBefore(core::LineNumber line) const
{
    const std::uint64_t matches = std::min(m_filter->lowerBound(line), m_layout.matchCount);
    const auto extras = static_cast<std::uint64_t>(
        std::ranges::lower_bound(m_layout.extraLines, line.get()) - m_layout.extraLines.begin());
    return matches + extras;
}

template <typename Fetch>
std::vector<DisplayLine> FilteredLineSource::collect(core::LineNumber first, core::LineCount count,
                                                     Fetch fetch) const
{
    LV_ASSERT(first.get() + count.get() <= lineCount().get());
    std::vector<DisplayLine> out;
    out.reserve(static_cast<std::size_t>(count.get()));
    std::uint64_t row = first.get();
    const std::uint64_t end = first.get() + count.get();
    while (row < end) {
        const std::uint64_t runStart = originalLineNumber(core::LineNumber(row)).get();
        const bool extra = isExtraRow(core::LineNumber(row));
        std::uint64_t runLength = 1;
        while (row + runLength < end &&
               originalLineNumber(core::LineNumber(row + runLength)).get() ==
                   runStart + runLength &&
               isExtraRow(core::LineNumber(row + runLength)) == extra) {
            ++runLength;
        }
        auto lines = fetch(core::LineNumber(runStart), core::LineCount(runLength));
        for (auto& line : lines) {
            line.extra = extra;
            out.push_back(std::move(line));
        }
        row += runLength;
    }
    return out;
}

std::vector<DisplayLine> FilteredLineSource::lines(core::LineNumber first,
                                                   core::LineCount count) const
{
    return collect(first, count, [this](core::LineNumber line, core::LineCount n) {
        return m_lines->lines(line, n);
    });
}

std::vector<DisplayLine> FilteredLineSource::plainLines(core::LineNumber first,
                                                        core::LineCount count) const
{
    return collect(first, count, [this](core::LineNumber line, core::LineCount n) {
        return m_lines->plainLines(line, n);
    });
}

std::optional<RowReader> FilteredLineSource::rowReader() const
{
    auto content = m_filter->document().content();
    if (!content) {
        return std::nullopt;
    }
    const auto snapshot = content->index.snapshot();
    // The match list is append-only per generation and safe to read from any thread; the
    // layout is copied.
    return RowReader{.content = std::move(content),
                     .snapshot = snapshot,
                     .lineForRow =
                         [matches = m_filter->matches(), layout = m_layout](core::LineNumber row) {
                             return core::LineNumber(
                                 targetOf(row.get(), layout.extraLines, layout.extraRows,
                                          [&matches](std::uint64_t index) {
                                              return matches->at(static_cast<std::size_t>(index));
                                          })
                                     .line);
                         },
                     .contiguous = false};
}

}  // namespace lv::model
