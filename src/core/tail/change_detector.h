#pragma once

#include "core/platform/file.h"
#include "core/util/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace lv::core
{

// Hash of the first `length` bytes of a file (at most kFingerprintBytes). Detects files that
// were rewritten from the start without becoming smaller (SPEC F-TAIL-5).
struct Fingerprint
{
    static constexpr std::size_t kFingerprintBytes = 4096;

    std::size_t length = 0;
    std::uint64_t hash = 0;

    bool operator==(const Fingerprint&) const = default;
};

// FNV-1a over `bytes`.
[[nodiscard]] Fingerprint fingerprintOf(std::string_view bytes) noexcept;

// What the document knew about its file after the last check.
struct FileSnapshot
{
    std::uint64_t size = 0;
    Fingerprint fingerprint;

    bool operator==(const FileSnapshot&) const = default;
};

enum class Change : std::uint8_t
{
    kUnchanged,
    kGrown,      // larger, same start: index the new bytes
    kTruncated,  // smaller, or the first bytes changed: re-index from the start
    kReplaced,   // the path refers to a different file (rotation): reopen
    kMissing,    // the path does not exist (deleted, or rotated away and not yet recreated)
};

[[nodiscard]] std::string_view toString(Change change) noexcept;

enum class PathState : std::uint8_t
{
    kExists,
    kMissing,
};

// One observation of the open file and its path; the input of classify().
struct Observation
{
    // Size of the open handle.
    std::uint64_t size = 0;
    // Fingerprint of the open handle over the first `last.fingerprint.length` bytes (or fewer
    // if the file is now shorter).
    Fingerprint fingerprint;
    PathState pathState = PathState::kExists;
    // Whether the path refers to the open file (identity comparison; meaningful if it exists).
    bool sameFile = true;
};

// Pure classification (ARCHITECTURE §3.7), in priority order: missing, replaced, truncated,
// grown, unchanged.
[[nodiscard]] Change classify(const FileSnapshot& last, const Observation& now) noexcept;

// Observes files for classify(). Stateless; safe to call from any thread.
class ChangeDetector
{
public:
    // Size and fingerprint of `file` now (after indexing, for the next check).
    [[nodiscard]] static Result<FileSnapshot> snapshot(const File& file);

    // Snapshot for the first `size` bytes of `file` (the indexed part), even if it is larger now.
    [[nodiscard]] static Result<FileSnapshot> snapshotAt(const File& file, std::uint64_t size);

    // Observes `open` and `path` and classifies the change since `last`. Errors come from the
    // open handle (I/O errors); a path that cannot be inspected for other reasons than not
    // existing is reported as an error too, so the caller simply retries on the next poll.
    // On Windows, a path whose file is pending deletion (access denied while the open handle
    // still works) counts as missing.
    [[nodiscard]] static Result<Change> check(const File& open, const std::filesystem::path& path,
                                              const FileSnapshot& last);

    // The underlying observation (exposed for tests and diagnostics).
    [[nodiscard]] static Result<Observation>
    observe(const File& open, const std::filesystem::path& path, const FileSnapshot& last);
};

}  // namespace lv::core
