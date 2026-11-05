#pragma once

#include "core/util/strong_types.h"
#include "model/line_source.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>

namespace lv::model
{

// LRU cache of decoded lines for the views, keyed by (generation, line number) (ARCHITECTURE §5).
// Entries of other generations are never returned; setGeneration() drops them.
//
// Thread-safety: UI thread only.
class LineCache
{
public:
    static constexpr std::size_t kDefaultCapacity = 10000;

    explicit LineCache(std::size_t capacity = kDefaultCapacity);

    // Drops all entries if `generation` differs from the current one.
    void setGeneration(std::uint64_t generation);

    [[nodiscard]] std::uint64_t generation() const noexcept { return m_generation; }

    // The cached line, marked as most recently used.
    [[nodiscard]] std::optional<DisplayLine> find(core::LineNumber line);

    // Inserts or replaces a line; evicts the least recently used one when full.
    void insert(core::LineNumber line, DisplayLine value);

    void clear();

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

    [[nodiscard]] std::uint64_t hits() const noexcept { return m_hits; }

    [[nodiscard]] std::uint64_t misses() const noexcept { return m_misses; }

private:
    struct Entry
    {
        DisplayLine value;
        std::list<std::uint64_t>::iterator lru;
    };

    std::size_t m_capacity;
    std::uint64_t m_generation = 0;
    std::unordered_map<std::uint64_t, Entry> m_entries;
    std::list<std::uint64_t> m_lru;  // most recently used first
    std::uint64_t m_hits = 0;
    std::uint64_t m_misses = 0;
};

}  // namespace lv::model
