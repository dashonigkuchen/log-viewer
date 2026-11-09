#include "model/bookmarks.h"

#include "model/document.h"

#include <iterator>

namespace lv::model
{

Bookmarks::Bookmarks(const Document& document, QObject* parent)
    : QObject(parent)
{
    connect(&document, &Document::reset, this, [this] {
        if (m_entries.empty()) {
            return;
        }
        const auto count = static_cast<quint64>(m_entries.size());
        m_entries.clear();
        emit changed();
        emit cleared(count);
    });
}

bool Bookmarks::contains(core::LineNumber line) const
{
    return m_entries.contains(line.get());
}

std::vector<Bookmarks::Entry> Bookmarks::entries() const
{
    std::vector<Entry> out;
    out.reserve(m_entries.size());
    for (const auto& [line, name] : m_entries) {
        out.push_back({.line = core::LineNumber(line), .name = name});
    }
    return out;
}

std::optional<QString> Bookmarks::name(core::LineNumber line) const
{
    const auto it = m_entries.find(line.get());
    return it == m_entries.end() ? std::nullopt : std::optional(it->second);
}

void Bookmarks::add(core::LineNumber line, const QString& name)
{
    m_entries.insert_or_assign(line.get(), name);
    emit changed();
}

bool Bookmarks::toggle(core::LineNumber line, const QString& lineText)
{
    if (m_entries.erase(line.get()) != 0) {
        emit changed();
        return false;
    }
    add(line, defaultName(lineText));
    return true;
}

void Bookmarks::rename(core::LineNumber line, const QString& name)
{
    const auto it = m_entries.find(line.get());
    if (it == m_entries.end() || it->second == name) {
        return;
    }
    it->second = name;
    emit changed();
}

void Bookmarks::remove(core::LineNumber line)
{
    if (m_entries.erase(line.get()) != 0) {
        emit changed();
    }
}

void Bookmarks::clear()
{
    if (m_entries.empty()) {
        return;
    }
    m_entries.clear();
    emit changed();
}

std::optional<core::LineNumber> Bookmarks::next(core::LineNumber line) const
{
    const auto it = m_entries.upper_bound(line.get());
    return it == m_entries.end() ? std::nullopt : std::optional(core::LineNumber(it->first));
}

std::optional<core::LineNumber> Bookmarks::previous(core::LineNumber line) const
{
    const auto it = m_entries.lower_bound(line.get());
    return it == m_entries.begin() ? std::nullopt
                                   : std::optional(core::LineNumber(std::prev(it)->first));
}

QString Bookmarks::defaultName(const QString& lineText)
{
    return lineText.simplified().left(kDefaultNameLength);
}

}  // namespace lv::model
