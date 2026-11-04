#include "model/file_log_sink.h"

#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QtLogging>

#include <utility>

namespace lv::model
{

FileLogSink::FileLogSink(QString path, std::int64_t maxBytes)
    : m_path(std::move(path))
    , m_maxBytes(maxBytes)
    , m_file(m_path)
{}

void FileLogSink::write(lv::core::LogLevel level, std::string_view message)
{
    QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8();
    line += " [";
    const auto levelName = lv::core::toString(level);
    line.append(levelName.data(), static_cast<qsizetype>(levelName.size()));
    line += "] ";
    line.append(message.data(), static_cast<qsizetype>(message.size()));
    line += '\n';

    const std::scoped_lock lock(m_mutex);
    rotateIfNeeded(line.size());
    if (!m_file.isOpen() && !m_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return;
    }
    m_file.write(line);
    m_file.flush();
}

void FileLogSink::rotateIfNeeded(std::int64_t incomingBytes)
{
    const std::int64_t current = m_file.isOpen() ? m_file.size() : QFileInfo(m_path).size();
    if (current == 0 || current + incomingBytes <= m_maxBytes) {
        return;
    }
    m_file.close();
    const QString rotated = m_path + QStringLiteral(".1");
    QFile::remove(rotated);
    QFile::rename(m_path, rotated);
}

namespace
{

void qtMessageHandler(QtMsgType type, const QMessageLogContext& /*context*/, const QString& message)
{
    lv::core::LogLevel level = lv::core::LogLevel::kInfo;
    switch (type) {
    case QtDebugMsg:
        level = lv::core::LogLevel::kDebug;
        break;
    case QtInfoMsg:
        level = lv::core::LogLevel::kInfo;
        break;
    case QtWarningMsg:
        level = lv::core::LogLevel::kWarning;
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        level = lv::core::LogLevel::kError;
        break;
    }
    const QByteArray utf8 = message.toUtf8();
    lv::core::log(level, std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

}  // namespace

void installQtMessageHandler()
{
    qInstallMessageHandler(qtMessageHandler);
}

}  // namespace lv::model
