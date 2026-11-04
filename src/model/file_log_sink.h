#pragma once

#include "core/util/log.h"

#include <QFile>
#include <QString>

#include <cstdint>
#include <mutex>
#include <string_view>

namespace lv::model
{

// Appends log messages to a text file and rotates it: when the file would grow beyond maxBytes,
// it is renamed to "<path>.1" (replacing an older one) and a new file is started.
// Line format: "2026-11-04T10:00:00.123Z [INFO] message".
// Thread-safety: write() may be called from any thread.
class FileLogSink
{
public:
    static constexpr std::int64_t kDefaultMaxBytes = std::int64_t{1024} * 1024;

    explicit FileLogSink(QString path, std::int64_t maxBytes = kDefaultMaxBytes);

    // Writes one line. Errors are ignored: logging must never break the application.
    void write(lv::core::LogLevel level, std::string_view message);

    [[nodiscard]] const QString& path() const noexcept { return m_path; }

private:
    void rotateIfNeeded(std::int64_t incomingBytes);

    QString m_path;
    std::int64_t m_maxBytes;
    std::mutex m_mutex;
    QFile m_file;
};

// Routes qDebug/qInfo/qWarning/qCritical/qFatal messages to lv::core::log.
void installQtMessageHandler();

}  // namespace lv::model
