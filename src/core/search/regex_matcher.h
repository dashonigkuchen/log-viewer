#pragma once

#include "core/search/matcher.h"

#include <tl/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct pcre2_real_code_8;

namespace lv::core
{

struct RegexOptions
{
    bool caseSensitive = false;
    // PCRE2 match limit per line (F-ROB-3); lines that exceed it are skipped.
    std::uint32_t matchLimit = 1'000'000;
    // PCRE2 depth limit per line (only used by the interpreter when JIT is unavailable).
    std::uint32_t depthLimit = 10'000;
};

// Error from compiling a pattern: PCRE2's message and the byte offset in the pattern.
struct PatternError
{
    std::string message;
    std::size_t offset = 0;

    friend bool operator==(const PatternError&, const PatternError&) = default;
};

enum class RegexOutcome : std::uint8_t
{
    kNoMatch,
    kMatch,
    // Match or depth limit hit, or the line is not valid UTF-8; counted in
    // MatchScratch::skippedLines by matches().
    kSkipped,
};

// PCRE2 regular expression matcher (SPEC F-FILT-3): UTF and Unicode properties (\w, case folding)
// on UTF-8 input, JIT compiled when available, with match limits so pathological patterns cannot
// freeze a search (F-ROB-3).
// Thread-safety: immutable, share freely; each thread needs its own MatchScratch.
class RegexMatcher final : public Matcher
{
public:
    [[nodiscard]] static tl::expected<std::unique_ptr<RegexMatcher>, PatternError>
    compile(std::string_view pattern, RegexOptions options = {});

    ~RegexMatcher() override;
    RegexMatcher(const RegexMatcher&) = delete;
    RegexMatcher& operator=(const RegexMatcher&) = delete;
    RegexMatcher(RegexMatcher&&) = delete;
    RegexMatcher& operator=(RegexMatcher&&) = delete;

    [[nodiscard]] const std::string& pattern() const noexcept { return m_pattern; }

    [[nodiscard]] const RegexOptions& options() const noexcept { return m_options; }

    // True if the pattern was JIT compiled.
    [[nodiscard]] bool jitCompiled() const noexcept;

    [[nodiscard]] RegexOutcome match(std::string_view line, MatchScratch& scratch) const;

    [[nodiscard]] bool matches(std::string_view line, MatchScratch& scratch) const override;
    void collectRanges(std::string_view line, MatchScratch& scratch,
                       std::vector<MatchRange>& out) const override;

private:
    struct CodeDeleter
    {
        void operator()(pcre2_real_code_8* code) const noexcept;
    };
    struct Scratch;

    RegexMatcher(std::string pattern, RegexOptions options,
                 std::unique_ptr<pcre2_real_code_8, CodeDeleter> code, bool jit);

    [[nodiscard]] Scratch& scratchFor(MatchScratch& scratch) const;

    std::string m_pattern;
    RegexOptions m_options;
    std::unique_ptr<pcre2_real_code_8, CodeDeleter> m_code;
    bool m_jit = false;
};

}  // namespace lv::core
