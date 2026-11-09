#include "model/mark_set.h"

#include "model/document.h"

#include <algorithm>

namespace lv::model
{

MarkSet::MarkSet(const Document& document, QObject* parent)
    : QObject(parent)
{
    connect(&document, &Document::reset, this, [this] {
        if (m_lines.empty()) {
            return;
        }
        const auto count = static_cast<quint64>(m_lines.size());
        m_lines.clear();
        emit changed();
        emit cleared(count);
    });
}

bool MarkSet::contains(core::LineNumber line) const
{
    return m_lines.contains(line.get());
}

std::optional<int> MarkSet::colorOf(core::LineNumber line) const
{
    const auto it = m_lines.find(line.get());
    return it == m_lines.end() ? std::nullopt : std::optional<int>(it->second);
}

std::vector<core::LineNumber> MarkSet::lines() const
{
    std::vector<core::LineNumber> out;
    out.reserve(m_lines.size());
    for (const auto& entry : m_lines) {
        out.emplace_back(entry.first);
    }
    return out;
}

void MarkSet::toggle(core::LineNumber line)
{
    if (m_lines.erase(line.get()) == 0) {
        m_lines.emplace(line.get(), std::uint8_t{0});
    }
    emit changed();
}

void MarkSet::toggle(std::span<const core::LineNumber> lines)
{
    if (lines.empty()) {
        return;
    }
    const bool allMarked =
        std::ranges::all_of(lines, [this](core::LineNumber line) { return contains(line); });
    for (const core::LineNumber line : lines) {
        if (allMarked) {
            m_lines.erase(line.get());
        } else {
            m_lines.try_emplace(line.get(), std::uint8_t{0});
        }
    }
    emit changed();
}

void MarkSet::toggleColor(std::span<const core::LineNumber> lines, int color)
{
    if (lines.empty() || color < 0 || color >= kColorCount) {
        return;
    }
    const bool allSame = std::ranges::all_of(
        lines, [this, color](core::LineNumber line) { return colorOf(line) == color; });
    for (const core::LineNumber line : lines) {
        if (allSame) {
            m_lines.erase(line.get());
        } else {
            m_lines.insert_or_assign(line.get(), static_cast<std::uint8_t>(color));
        }
    }
    emit changed();
}

void MarkSet::clear()
{
    if (m_lines.empty()) {
        return;
    }
    m_lines.clear();
    emit changed();
}

std::optional<core::LineNumber> MarkSet::next(core::LineNumber line) const
{
    const auto it = m_lines.upper_bound(line.get());
    return it == m_lines.end() ? std::nullopt : std::optional(core::LineNumber(it->first));
}

std::optional<core::LineNumber> MarkSet::previous(core::LineNumber line) const
{
    const auto it = m_lines.lower_bound(line.get());
    return it == m_lines.begin() ? std::nullopt
                                 : std::optional(core::LineNumber(std::prev(it)->first));
}

}  // namespace lv::model
