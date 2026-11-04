#pragma once

#include "core/util/result.h"
#include "core/util/strong_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace lv::core
{

// Identifies a file independently of its path (ARCHITECTURE §3.1): Windows volume serial number
// plus the 128-bit file ID, POSIX st_dev plus st_ino. Two open handles refer to the same file if
// and only if their identities are equal. Used to detect log rotation.
struct FileIdentity
{
    std::uint64_t volume = 0;
    std::array<std::uint8_t, 16> fileId{};

    bool operator==(const FileIdentity&) const = default;
};

// A file opened read-only with full sharing (SPEC F-FILE-2): other processes may keep writing,
// truncating, renaming, and deleting it. Reads are positional and never use a shared file
// pointer. The file is never memory-mapped (ADR 0005).
//
// Thread-safety: all const member functions may be called concurrently from several threads.
// Moving or destroying a File must not race with other calls.
class File
{
public:
    // Opens `path` for reading. Errors: kNotFound, kPermissionDenied, kIsDirectory, kIoError.
    [[nodiscard]] static Result<File> open(const std::filesystem::path& path);

    // Identity of the file that `path` currently refers to; opens it briefly with full sharing.
    [[nodiscard]] static Result<FileIdentity> identityOfPath(const std::filesystem::path& path);

    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    ~File();

    // Reads up to buffer.size() bytes starting at `offset`. Returns the number of bytes read,
    // which is smaller than buffer.size() only at the end of the file (0 at or past the end).
    [[nodiscard]] Result<std::size_t> readAt(ByteOffset offset, std::span<std::byte> buffer) const;

    // Current size, queried from the open handle (not from the directory entry, which NTFS
    // updates lazily while a writer keeps the file open).
    [[nodiscard]] Result<std::uint64_t> size() const;

    [[nodiscard]] Result<FileIdentity> identity() const;

    // The path the file was opened with.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    using NativeHandle = std::intptr_t;
    static constexpr NativeHandle kInvalidHandle = -1;

    File(NativeHandle handle, std::filesystem::path path) noexcept;
    void close() noexcept;

    NativeHandle m_handle = kInvalidHandle;
    std::filesystem::path m_path;
};

}  // namespace lv::core
