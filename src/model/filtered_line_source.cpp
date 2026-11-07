#include "model/filtered_line_source.h"

#include "core/util/assert.h"
#include "model/document.h"
#include "model/filtered_document.h"

#include <utility>

namespace lv::model
{

FilteredLineSource::FilteredLineSource(const FilteredDocument& filter,
                                       const DocumentLineSource& lines, QObject* parent)
    : LineSource(parent)
    , m_filter(&filter)
    , m_lines(&lines)
{
    connect(&filter, &FilteredDocument::matchesAppended, this, &LineSource::linesAppended);
    connect(&filter, &FilteredDocument::reset, this, &LineSource::reset);
}

core::LineCount FilteredLineSource::lineCount() const
{
    return core::LineCount(m_filter->matchCount());
}

core::LineNumber FilteredLineSource::originalLineNumber(core::LineNumber row) const
{
    return m_filter->matchAt(row.get());
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
        const std::uint64_t runStart = m_filter->matchAt(row).get();
        std::uint64_t runLength = 1;
        while (row + runLength < end &&
               m_filter->matchAt(row + runLength).get() == runStart + runLength) {
            ++runLength;
        }
        auto lines = fetch(core::LineNumber(runStart), core::LineCount(runLength));
        for (auto& line : lines) {
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
    // The match list is append-only per generation and safe to read from any thread.
    auto matches = m_filter->matches();
    return RowReader{.content = std::move(content),
                     .snapshot = snapshot,
                     .lineForRow =
                         [matches = std::move(matches)](core::LineNumber row) {
                             return core::LineNumber(
                                 matches->at(static_cast<std::size_t>(row.get())));
                         },
                     .contiguous = false};
}

}  // namespace lv::model
