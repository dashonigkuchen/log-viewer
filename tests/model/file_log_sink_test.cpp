#include "core/util/log.h"
#include "model/file_log_sink.h"

#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QtLogging>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

namespace
{

QString readAll(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QString::fromUtf8(file.readAll());
}

}  // namespace

TEST_CASE("FileLogSink appends formatted lines", "[model][log]")
{
    const QTemporaryDir dir;
    REQUIRE(dir.isValid());
    lv::model::FileLogSink sink(dir.filePath(QStringLiteral("app.log")));

    sink.write(lv::core::LogLevel::kInfo, "hello");
    sink.write(lv::core::LogLevel::kError, "Ümlaut");

    const QStringList lines = readAll(sink.path()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].endsWith(QStringLiteral(" [INFO] hello")));
    CHECK(lines[1].endsWith(QStringLiteral(" [ERROR] Ümlaut")));
    // ISO 8601 UTC timestamp with milliseconds, e.g. 2026-11-04T10:00:00.123Z
    CHECK(lines[0].indexOf(QLatin1Char(' ')) == 24);
    CHECK(lines[0].at(23) == QLatin1Char('Z'));
}

TEST_CASE("FileLogSink rotates when the file would exceed the limit", "[model][log]")
{
    const QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    lv::model::FileLogSink sink(path, 200);

    for (int i = 0; i < 10; ++i) {
        sink.write(lv::core::LogLevel::kInfo, std::string(40, static_cast<char>('a' + i)));
    }

    CHECK(QFile::exists(path + QStringLiteral(".1")));
    CHECK_FALSE(QFile::exists(path + QStringLiteral(".2")));
    CHECK(QFile(path).size() <= 200);
    CHECK(QFile(path + QStringLiteral(".1")).size() <= 200);
    CHECK(readAll(path).contains(QString(40, QLatin1Char('j'))));
}

TEST_CASE("FileLogSink writes a single oversized line instead of dropping it", "[model][log]")
{
    const QTemporaryDir dir;
    REQUIRE(dir.isValid());
    lv::model::FileLogSink sink(dir.filePath(QStringLiteral("app.log")), 10);
    sink.write(lv::core::LogLevel::kInfo, "this line is longer than the limit");
    CHECK(readAll(sink.path()).contains(QStringLiteral("longer than the limit")));
}

TEST_CASE("FileLogSink ignores an unwritable path", "[model][log]")
{
    lv::model::FileLogSink sink(QStringLiteral("/nonexistent-dir-lv/sub/app.log"));
    sink.write(lv::core::LogLevel::kInfo, "lost");
    SUCCEED();
}

TEST_CASE("Qt messages are forwarded to the core log", "[model][log]")
{
    std::vector<std::pair<lv::core::LogLevel, std::string>> received;
    lv::core::setLogSink([&received](lv::core::LogLevel level, std::string_view message) {
        received.emplace_back(level, std::string(message));
    });
    lv::model::installQtMessageHandler();

    qWarning("careful %d", 42);

    qInstallMessageHandler(nullptr);
    lv::core::setLogSink({});
    REQUIRE(received.size() == 1);
    CHECK(received[0].first == lv::core::LogLevel::kWarning);
    CHECK(received[0].second == "careful 42");
}
