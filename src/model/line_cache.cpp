#include "model/line_cache.h"

#include "core/util/assert.h"

#include <utility>

namespace lv::model
{

LineCache::LineCache(std::size_t capacity)
    : m_capacity(capacity)
{
    LV_ASSERT(capacity > 0);
}

void LineCache::setGeneration(std::uint64_t generation)
{
    if (generation != m_generation) {
        clear();
        m_generation = generation;
    }
}

std::optional<DisplayLine> LineCache::find(core::LineNumber line)
{
    const auto found = m_entries.find(line.get());
    if (found == m_entries.end()) {
        ++m_misses;
        return std::nullopt;
    }
    ++m_hits;
    m_lru.splice(m_lru.begin(), m_lru, found->second.lru);
    return found->second.value;
}

void LineCache::insert(core::LineNumber line, DisplayLine value)
{
    const auto [found, inserted] = m_entries.try_emplace(line.get());
    found->second.value = std::move(value);
    if (inserted) {
        m_lru.push_front(line.get());
        found->second.lru = m_lru.begin();
    } else {
        m_lru.splice(m_lru.begin(), m_lru, found->second.lru);
    }
    while (m_entries.size() > m_capacity) {
        m_entries.erase(m_lru.back());
        m_lru.pop_back();
    }
}

void LineCache::clear()
{
    m_entries.clear();
    m_lru.clear();
}

}  // namespace lv::model
