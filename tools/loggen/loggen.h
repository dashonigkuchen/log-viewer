#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Deterministic generator for synthetic log files (docs/PERFORMANCE.md §2).
// The same profile and seed always produce byte-identical output on every platform:
// the generator uses its own PRNG, integer arithmetic only, and no locale-dependent formatting.
// Changing the output format changes all benchmark data; update the golden hashes in
// tests/tools/loggen_test.cpp in the same PR and say so in the PR description.
namespace lv::loggen
{

inline constexpr std::uint64_t kKiB = 1024;
inline constexpr std::uint64_t kMiB = 1024 * kKiB;
inline constexpr std::uint64_t kGiB = 1024 * kMiB;

enum class Profile : std::uint8_t
{
    kSmall,   // 50 MiB application log, ASCII, average line 130 bytes
    kLarge,   // 1 GiB, same content as small
    kHuge,    // 10 GiB, same content as small
    kShort,   // 1 GiB of ~20 byte lines
    kLong,    // 200 MiB of 3-5 MiB lines
    kUtf16,   // 200 MiB, cp1252-style content encoded as UTF-16 LE with BOM
    kCp1252,  // 50 MiB, application log with German umlauts in Windows-1252
};

[[nodiscard]] std::optional<Profile> parseProfile(std::string_view name) noexcept;
[[nodiscard]] std::string_view profileName(Profile profile) noexcept;
[[nodiscard]] std::uint64_t defaultSize(Profile profile) noexcept;

// Parses a byte size: "4096", "64K", "5M", "1G" (binary units, case-insensitive).
[[nodiscard]] std::optional<std::uint64_t> parseSize(std::string_view text) noexcept;

// xoshiro256** seeded through SplitMix64. Portable and deterministic.
class Random
{
public:
    explicit Random(std::uint64_t seed) noexcept;

    [[nodiscard]] std::uint64_t next() noexcept;

    // Uniform value in [0, bound). bound must be > 0. Unbiased (rejection sampling).
    [[nodiscard]] std::uint64_t below(std::uint64_t bound) noexcept;

    // Uniform value in [low, high].
    [[nodiscard]] std::uint64_t between(std::uint64_t low, std::uint64_t high) noexcept;

private:
    std::array<std::uint64_t, 4> m_state{};
};

// Produces lines of one profile. Thread-safety: none (one generator per thread).
class Generator
{
public:
    Generator(Profile profile, std::uint64_t seed);

    // Bytes that start every file of this profile (the UTF-16 BOM, otherwise empty).
    [[nodiscard]] std::string_view fileHeader() const noexcept;

    // Appends the next line, including its "\n" terminator, in the profile's encoding.
    void appendLine(std::string& out);

private:
    enum class Level : std::uint8_t
    {
        kDebug,
        kInfo,
        kWarn,
        kError,
    };

    [[nodiscard]] Level pickLevel() noexcept;
    void buildTextLine(std::uint64_t contentLength);
    void buildShortLine(std::uint64_t contentLength);
    void appendTimestamp();
    void appendMessage(Level level, std::uint64_t contentLength);

    Profile m_profile;
    Random m_random;
    bool m_latin1Words = false;
    std::uint64_t m_timeMs = 0;
    std::uint64_t m_counter = 0;
    std::string m_line;
};

struct WriteOptions
{
    Profile profile = Profile::kSmall;
    std::uint64_t seed = 1;
    std::uint64_t sizeBytes = 0;  // 0 = default size of the profile; ignored in live mode
    std::filesystem::path out;
    std::uint64_t appendRateBytesPerSecond = 0;  // > 0 = live mode: append, flush every write
    std::uint64_t durationMs = 0;                // live mode duration
    std::uint64_t rotateEveryBytes = 0;          // rename to "<out>.1" and start a new file
    std::uint64_t truncateEveryBytes = 0;        // truncate the file to zero and continue
};

struct WriteResult
{
    std::uint64_t bytes = 0;  // all bytes written, including headers
    std::uint64_t lines = 0;
    std::uint64_t rotations = 0;
    std::uint64_t truncations = 0;
    std::string error;  // empty on success

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

// Normal mode: creates or replaces `out` and writes lines until at least sizeBytes are written
// (the last line is always complete). Live mode (appendRateBytesPerSecond > 0): appends to `out`
// at the given rate for durationMs, flushing after every write, like a logging application.
[[nodiscard]] WriteResult writeLog(const WriteOptions& options);

}  // namespace lv::loggen
