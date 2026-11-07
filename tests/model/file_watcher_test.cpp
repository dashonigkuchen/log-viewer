#include "log_writer.h"
#include "model/file_watcher.h"
#include "temp_dir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <atomic>
#include <filesystem>
#include <thread>

using lv::model::FileWatcher;
using lv::test::LogWriter;
using lv::test::TempDir;

namespace
{

// Runs the event loop until `spy` has more than `count` signals or `timeoutMs` passed.
bool waitForMore(const QSignalSpy& spy, qsizetype count, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (spy.count() <= count && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return spy.count() > count;
}

void spin(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

}  // namespace

TEST_CASE("file watcher reports appends with notifications and with polling only", "[model][watch]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    LogWriter writer(path, true);
    writer.append("start\n");
    const bool notifications = GENERATE(true, false);
    CAPTURE(notifications);
    FileWatcher watcher(path);
    watcher.setNotificationsEnabled(notifications);
    watcher.setPollInterval(100);
    const QSignalSpy changed(&watcher, &FileWatcher::changed);
    spin(150);
    const auto before = changed.count();

    std::jthread thread([&writer] { writer.append("line from another thread\n"); });
    thread.join();
    CHECK(waitForMore(changed, before, 3000));
}

TEST_CASE("file watcher reports deletion and recreation", "[model][watch]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    {
        LogWriter writer(path, true);
        writer.append("old\n");
    }
    FileWatcher watcher(path);
    watcher.setPollInterval(100);
    const QSignalSpy changed(&watcher, &FileWatcher::changed);
    std::filesystem::remove(path);
    REQUIRE(waitForMore(changed, 0, 3000));
    spin(150);
    const auto afterDelete = changed.count();
    {
        LogWriter writer(path, true);
        writer.append("new\n");
    }
    CHECK(waitForMore(changed, afterDelete, 3000));
    // After recreation, appends are seen again.
    spin(150);
    const auto afterRecreate = changed.count();
    LogWriter writer(path);
    writer.append("more\n");
    CHECK(waitForMore(changed, afterRecreate, 3000));
}

TEST_CASE("a burst of appends yields a bounded number of signals", "[model][watch]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    LogWriter writer(path, true);
    FileWatcher watcher(path);
    watcher.setPollInterval(100);
    const QSignalSpy changed(&watcher, &FileWatcher::changed);
    std::atomic<bool> done = false;
    QElapsedTimer timer;
    timer.start();
    std::jthread thread([&] {
        for (int i = 0; i < 10000; ++i) {
            writer.append("burst line with some text in it\n");
        }
        done = true;
    });
    while (!done) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    thread.join();
    spin(300);
    const qint64 elapsed = timer.elapsed();
    // At most one signal per coalescing window (plus slack for timer jitter).
    CHECK(changed.count() >= 1);
    CHECK(changed.count() <= (elapsed / FileWatcher::kCoalesceMs) + 3);
    CHECK(watcher.signalCount() == static_cast<std::uint64_t>(changed.count()));
}

TEST_CASE("the handle size probe catches growth that the path does not show", "[model][watch]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    {
        LogWriter writer(path, true);
        writer.append("x\n");
    }
    std::uint64_t handleSize = 2;
    FileWatcher watcher(path);
    watcher.setNotificationsEnabled(false);
    watcher.setPollInterval(100);
    watcher.setHandleSizeProbe([&handleSize] { return std::optional(handleSize); });
    const QSignalSpy changed(&watcher, &FileWatcher::changed);
    spin(250);
    CHECK(changed.count() == 0);  // nothing changed
    handleSize = 10;              // as if NTFS had not updated the directory entry yet
    CHECK(waitForMore(changed, 0, 3000));
}

TEST_CASE("pause stops signals and resume reports once", "[model][watch]")
{
    const TempDir dir;
    const auto path = dir / "app.log";
    LogWriter writer(path, true);
    FileWatcher watcher(path);
    watcher.setPollInterval(100);
    const QSignalSpy changed(&watcher, &FileWatcher::changed);
    watcher.pause();
    CHECK(watcher.isPaused());
    writer.append("while paused\n");
    spin(300);
    CHECK(changed.count() == 0);
    watcher.resume();
    CHECK(waitForMore(changed, 0, 1000));
    watcher.setPollInterval(5);
    CHECK(watcher.pollInterval() == FileWatcher::kMinPollMs);
}
