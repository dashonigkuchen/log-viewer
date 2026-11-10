#include "core/tail/change_detector.h"

#include "core/util/strong_types.h"

#include <algorithm>
#include <array>
#include <span>

namespace lv::core
{
namespace
{

// Fingerprint of the first `length` bytes of `file` (fewer if it is shorter).
Result<Fingerprint> readFingerprint(const File& file, std::size_t length)
{
    std::array<std::byte, Fingerprint::kFingerprintBytes> buffer{};
    const std::size_t wanted = std::min(length, buffer.size());
    std::size_t filled = 0;
    while (filled < wanted) {
        const auto read =
            file.readAt(ByteOffset(filled), std::span(buffer).subspan(filled, wanted - filled));
        if (!read) {
            return std::unexpected(read.error());
        }
        if (*read == 0) {
            break;  // end of file
        }
        filled += *read;
    }
    return fingerprintOf(std::string_view(reinterpret_cast<const char*>(buffer.data()),  // NOLINT
                                          filled));
}

}  // namespace

Fingerprint fingerprintOf(std::string_view bytes) noexcept
{
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffset;
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= kPrime;
    }
    return {.length = bytes.size(), .hash = hash};
}

std::string_view toString(Change change) noexcept
{
    switch (change) {
    case Change::kUnchanged:
        return "unchanged";
    case Change::kGrown:
        return "grown";
    case Change::kTruncated:
        return "truncated";
    case Change::kReplaced:
        return "replaced";
    case Change::kMissing:
        return "missing";
    }
    return "unknown";
}

Change classify(const FileSnapshot& last, const Observation& now) noexcept
{
    if (now.pathState == PathState::kMissing) {
        return Change::kMissing;
    }
    if (!now.sameFile) {
        return Change::kReplaced;
    }
    if (now.size < last.size || now.fingerprint != last.fingerprint) {
        return Change::kTruncated;
    }
    if (now.size > last.size) {
        return Change::kGrown;
    }
    return Change::kUnchanged;
}

Result<FileSnapshot> ChangeDetector::snapshot(const File& file)
{
    const auto size = file.size();
    if (!size) {
        return std::unexpected(size.error());
    }
    const auto fingerprint = readFingerprint(file, static_cast<std::size_t>(std::min<std::uint64_t>(
                                                       *size, Fingerprint::kFingerprintBytes)));
    if (!fingerprint) {
        return std::unexpected(fingerprint.error());
    }
    return FileSnapshot{.size = *size, .fingerprint = *fingerprint};
}

Result<FileSnapshot> ChangeDetector::snapshotAt(const File& file, std::uint64_t size)
{
    const auto fingerprint = readFingerprint(file, static_cast<std::size_t>(std::min<std::uint64_t>(
                                                       size, Fingerprint::kFingerprintBytes)));
    if (!fingerprint) {
        return std::unexpected(fingerprint.error());
    }
    return FileSnapshot{.size = size, .fingerprint = *fingerprint};
}

Result<Observation> ChangeDetector::observe(const File& open, const std::filesystem::path& path,
                                            const FileSnapshot& last)
{
    Observation now;
    const auto size = open.size();
    if (!size) {
        return std::unexpected(size.error());
    }
    now.size = *size;
    const auto fingerprint = readFingerprint(open, last.fingerprint.length);
    if (!fingerprint) {
        return std::unexpected(fingerprint.error());
    }
    now.fingerprint = *fingerprint;

    const auto pathIdentity = File::identityOfPath(path);
    if (!pathIdentity) {
        const ErrorCode code = pathIdentity.error().code;
#if defined(_WIN32)
        const bool missing = code == ErrorCode::kNotFound || code == ErrorCode::kPermissionDenied;
#else
        const bool missing = code == ErrorCode::kNotFound;
#endif
        if (!missing) {
            return std::unexpected(pathIdentity.error());
        }
        now.pathState = PathState::kMissing;
        return now;
    }
    const auto openIdentity = open.identity();
    if (!openIdentity) {
        return std::unexpected(openIdentity.error());
    }
    now.sameFile = *openIdentity == *pathIdentity;
    return now;
}

Result<Change> ChangeDetector::check(const File& open, const std::filesystem::path& path,
                                     const FileSnapshot& last)
{
    const auto now = observe(open, path, last);
    if (!now) {
        return std::unexpected(now.error());
    }
    return classify(last, *now);
}

}  // namespace lv::core
