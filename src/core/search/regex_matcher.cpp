#include "core/search/regex_matcher.h"

#define PCRE2_CODE_UNIT_WIDTH 8  // NOLINT(cppcoreguidelines-macro-usage): required by pcre2.h
#include <pcre2.h>

#include <array>
#include <utility>

namespace lv::core
{

namespace
{

constexpr std::size_t kJitStackStart = std::size_t{32} * 1024;
constexpr std::size_t kJitStackMax = std::size_t{1024} * 1024;

std::string errorMessage(int code)
{
    std::array<PCRE2_UCHAR, 256> buffer{};
    const int length = pcre2_get_error_message(code, buffer.data(), buffer.size());
    if (length < 0) {
        return "unknown regex error " + std::to_string(code);
    }
    return {reinterpret_cast<const char*>(buffer.data()),  // NOLINT(*-reinterpret-cast)
            static_cast<std::size_t>(length)};
}

PCRE2_SPTR toPcre(std::string_view text) noexcept
{
    return reinterpret_cast<PCRE2_SPTR>(text.data());  // NOLINT(*-reinterpret-cast)
}

bool isUtf8Continuation(char c) noexcept
{
    return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U;
}

}  // namespace

// Per-thread PCRE2 state for one matcher.
struct RegexMatcher::Scratch final : ScratchData
{
    pcre2_match_data* data = nullptr;
    pcre2_match_context* context = nullptr;
    pcre2_jit_stack* stack = nullptr;

    Scratch(const pcre2_code* code, const RegexOptions& options, bool jit)
        : data(pcre2_match_data_create_from_pattern(code, nullptr))
        , context(pcre2_match_context_create(nullptr))
    {
        pcre2_set_match_limit(context, options.matchLimit);
        pcre2_set_depth_limit(context, options.depthLimit);
        if (jit) {
            stack = pcre2_jit_stack_create(kJitStackStart, kJitStackMax, nullptr);
            pcre2_jit_stack_assign(context, nullptr, stack);
        }
    }

    ~Scratch() override
    {
        pcre2_jit_stack_free(stack);
        pcre2_match_context_free(context);
        pcre2_match_data_free(data);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
};

void RegexMatcher::CodeDeleter::operator()(pcre2_real_code_8* code) const noexcept
{
    pcre2_code_free(code);
}

tl::expected<std::unique_ptr<RegexMatcher>, PatternError>
RegexMatcher::compile(std::string_view pattern, RegexOptions options)
{
    std::uint32_t flags = PCRE2_UTF | PCRE2_UCP;
    if (!options.caseSensitive) {
        flags |= PCRE2_CASELESS;
    }
    int errorCode = 0;
    PCRE2_SIZE errorOffset = 0;
    std::unique_ptr<pcre2_real_code_8, CodeDeleter> code(
        pcre2_compile(toPcre(pattern), pattern.size(), flags, &errorCode, &errorOffset, nullptr));
    if (!code) {
        return tl::unexpected(PatternError{.message = errorMessage(errorCode),
                                           .offset = static_cast<std::size_t>(errorOffset)});
    }
    const bool jit = pcre2_jit_compile(code.get(), PCRE2_JIT_COMPLETE) == 0;
    return std::unique_ptr<RegexMatcher>(
        new RegexMatcher(std::string(pattern), options, std::move(code), jit));
}

RegexMatcher::RegexMatcher(std::string pattern, RegexOptions options,
                           std::unique_ptr<pcre2_real_code_8, CodeDeleter> code, bool jit)
    : m_pattern(std::move(pattern))
    , m_options(options)
    , m_code(std::move(code))
    , m_jit(jit)
{}

RegexMatcher::~RegexMatcher() = default;

bool RegexMatcher::jitCompiled() const noexcept
{
    std::size_t size = 0;
    return m_jit && pcre2_pattern_info(m_code.get(), PCRE2_INFO_JITSIZE, &size) == 0 && size > 0;
}

RegexMatcher::Scratch& RegexMatcher::scratchFor(MatchScratch& scratch) const
{
    return scratch.get<Scratch>(
        this, [this] { return std::make_unique<Scratch>(m_code.get(), m_options, m_jit); });
}

RegexOutcome RegexMatcher::match(std::string_view line, MatchScratch& scratch) const
{
    const Scratch& state = scratchFor(scratch);
    const int rc =
        pcre2_match(m_code.get(), toPcre(line), line.size(), 0, 0, state.data, state.context);
    if (rc >= 0) {
        return RegexOutcome::kMatch;
    }
    if (rc == PCRE2_ERROR_NOMATCH) {
        return RegexOutcome::kNoMatch;
    }
    // Match/depth/heap limit, JIT stack limit, or invalid UTF-8.
    return RegexOutcome::kSkipped;
}

bool RegexMatcher::matches(std::string_view line, MatchScratch& scratch) const
{
    const RegexOutcome outcome = match(line, scratch);
    if (outcome == RegexOutcome::kSkipped) {
        ++scratch.skippedLines;
    }
    return outcome == RegexOutcome::kMatch;
}

void RegexMatcher::collectRanges(std::string_view line, MatchScratch& scratch,
                                 std::vector<MatchRange>& out) const
{
    const Scratch& state = scratchFor(scratch);
    std::size_t offset = 0;
    while (offset <= line.size()) {
        const int rc = pcre2_match(m_code.get(), toPcre(line), line.size(), offset,
                                   offset == 0 ? 0 : PCRE2_NO_UTF_CHECK, state.data, state.context);
        if (rc < 0) {
            return;  // no further match, or a limit was hit: keep what we have
        }
        const PCRE2_SIZE* vector = pcre2_get_ovector_pointer(state.data);
        const std::size_t begin = vector[0];
        const std::size_t end = vector[1];  // NOLINT(*-pointer-arithmetic)
        if (end > begin) {
            out.push_back({begin, end});
            offset = end;
            continue;
        }
        // Empty match: advance by one code point.
        offset = end + 1;
        while (offset < line.size() && isUtf8Continuation(line[offset])) {
            ++offset;
        }
    }
}

}  // namespace lv::core
