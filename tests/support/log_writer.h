#pragma once

#include <filesystem>
#include <string_view>

namespace lv::test
{

// Simulates a logging application writing to a file: opens for writing with the sharing mode
// typical loggers use (Windows: FILE_SHARE_READ only; POSIX: plain open), appends, truncates.
class LogWriter
{
public:
    // Opens or creates `path`. With `truncate`, existing content is discarded.
    explicit LogWriter(const std::filesystem::path& path, bool truncate = false);
    LogWriter(const LogWriter&) = delete;
    LogWriter& operator=(const LogWriter&) = delete;
    LogWriter(LogWriter&&) = delete;
    LogWriter& operator=(LogWriter&&) = delete;
    ~LogWriter();

    void append(std::string_view text);
    void truncate();
    void close();

private:
    std::intptr_t m_handle = -1;
};

}  // namespace lv::test
