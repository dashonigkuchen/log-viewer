#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace lv::core
{

// Half-open byte range [begin, end) of a match within one line (not a file offset range;
// see ByteRange in strong_types.h for those).
struct MatchRange
{
    std::size_t begin = 0;
    std::size_t end = 0;

    friend bool operator==(const MatchRange&, const MatchRange&) = default;
};

// Base class for per-thread state of one matcher (for example PCRE2 match data).
class ScratchData
{
public:
    ScratchData() = default;
    virtual ~ScratchData() = default;
    ScratchData(const ScratchData&) = delete;
    ScratchData& operator=(const ScratchData&) = delete;
    ScratchData(ScratchData&&) = delete;
    ScratchData& operator=(ScratchData&&) = delete;
};

// Mutable per-thread state for matching (ARCHITECTURE §3.6). Each worker thread owns one
// MatchScratch and passes it to every call; matchers store their state keyed by their address.
// Thread-safety: one thread at a time.
class MatchScratch
{
public:
    // Lines that could not be evaluated (regex limits, invalid UTF-8; F-ROB-3). Incremented by
    // Matcher::matches, read and reset by the search engine.
    std::uint64_t skippedLines = 0;

    // Returns the data stored for `owner`, creating it with `create()` on first use.
    template <typename T, typename Create>
    T& get(const void* owner, Create&& create)
    {
        for (auto& [key, data] : m_entries) {
            if (key == owner) {
                return static_cast<T&>(*data);
            }
        }
        std::unique_ptr<T> created = std::forward<Create>(create)();
        T& result = *created;
        m_entries.emplace_back(owner, std::move(created));
        return result;
    }

private:
    std::vector<std::pair<const void*, std::unique_ptr<ScratchData>>> m_entries;
};

// Decides whether a line (UTF-8, without line terminator) matches. Matchers are immutable after
// construction and can be shared between threads; all mutable state lives in MatchScratch.
class Matcher
{
public:
    Matcher() = default;
    virtual ~Matcher() = default;
    Matcher(const Matcher&) = delete;
    Matcher& operator=(const Matcher&) = delete;
    Matcher(Matcher&&) = delete;
    Matcher& operator=(Matcher&&) = delete;

    [[nodiscard]] virtual bool matches(std::string_view line, MatchScratch& scratch) const = 0;

    // Appends all non-overlapping matches in `line`, left to right (for highlighting). Matches
    // of empty length are not reported.
    virtual void collectRanges(std::string_view line, MatchScratch& scratch,
                               std::vector<MatchRange>& out) const = 0;

    // Chunk prefilter (ARCHITECTURE §3.6, M3-11): true if nextCandidate() can skip over text
    // spanning many lines. A line can only match if it contains a candidate position; the
    // search engine confirms every candidate line with matches().
    [[nodiscard]] virtual bool hasCandidateScan() const noexcept { return false; }

    // First byte at or after `from` in `text` (UTF-8, may contain line feeds) where a match
    // could start, or std::string_view::npos. Only called if hasCandidateScan().
    [[nodiscard]] virtual std::size_t nextCandidate(std::string_view text, std::size_t from) const
    {
        return from < text.size() ? from : std::string_view::npos;
    }
};

}  // namespace lv::core
