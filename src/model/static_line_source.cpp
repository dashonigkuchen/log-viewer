#include "model/static_line_source.h"

#include "core/util/assert.h"

#include <utility>

namespace lv::model
{

StaticLineSource::StaticLineSource(QStringList lines, QObject* parent)
    : LineSource(parent)
    , m_lines(std::move(lines))
{}

void StaticLineSource::setLines(QStringList lines)
{
    m_lines = std::move(lines);
    emit reset();
}

core::LineCount StaticLineSource::lineCount() const
{
    return core::LineCount(static_cast<std::uint64_t>(m_lines.size()));
}

core::LineNumber StaticLineSource::originalLineNumber(core::LineNumber row) const
{
    return row;
}

std::vector<DisplayLine> StaticLineSource::lines(core::LineNumber first,
                                                 core::LineCount count) const
{
    LV_ASSERT(first.get() + count.get() <= lineCount().get());
    std::vector<DisplayLine> out;
    out.reserve(static_cast<std::size_t>(count.get()));
    for (std::uint64_t row = first.get(); row < first.get() + count.get(); ++row) {
        DisplayLine line;
        line.text = m_lines.at(static_cast<qsizetype>(row));
        out.push_back(std::move(line));
    }
    return out;
}

std::vector<DisplayLine> StaticLineSource::plainLines(core::LineNumber first,
                                                      core::LineCount count) const
{
    return lines(first, count);
}

}  // namespace lv::model
