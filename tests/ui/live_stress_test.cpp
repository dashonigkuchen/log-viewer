// Live stress test (M4-07, P-TAIL-2, P-UI-2): a file grows at a high rate while a filter and
// follow mode are active. Measures the longest UI-thread stall and the memory growth.
//
// Labeled `slow`: runs in the nightly job, not in PR CI. Manual run, e.g. on Windows:
//   lv_ui_stress_tests -o -,txt
// Environment: LV_STRESS_RATE_MB (default 50), LV_STRESS_SECONDS (default 30).

#include "core/index/line_index.h"
#include "loggen/loggen.h"
#include "model/document.h"
#include "model/filtered_document.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
// psapi.h needs windows.h first.
#include <psapi.h>
#endif

namespace
{

std::uint64_t envNumber(const char* name, std::uint64_t fallback)
{
    // qEnvironmentVariable: portable and not deprecated by MSVC (unlike std::getenv).
    bool ok = false;
    const auto value = qEnvironmentVariable(name).toULongLong(&ok);
    return ok ? value : fallback;
}

// Resident memory of this process in bytes (0 if unknown).
std::uint64_t residentBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        return counters.WorkingSetSize;
    }
    return 0;
#else
    std::ifstream statm("/proc/self/statm");
    std::uint64_t pages = 0;
    std::uint64_t resident = 0;
    statm >> pages >> resident;
    return resident * 4096;
#endif
}

}  // namespace

class LiveStressTest : public QObject
{
    Q_OBJECT

private slots:

    static void growingFileWithFilterAndFollow()
    {
        const std::uint64_t rateMb = envNumber("LV_STRESS_RATE_MB", 50);
        const std::uint64_t seconds = envNumber("LV_STRESS_SECONDS", 30);
        const lv::test::TempDir dir;
        const auto path = dir / "live.log";
        {
            std::ofstream create(path, std::ios::binary);
            create << "start\n";
        }

        lv::ui::DocumentTab tab(path);
        tab.document().setPollInterval(100);
        tab.resize(1000, 700);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QElapsedTimer ready;
        ready.start();
        while (tab.document().state() != lv::model::Document::State::kReady &&
               ready.elapsed() < 10000) {
            QApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        QVERIFY(!tab.filter().apply({.text = "ERROR"}).has_value());
        tab.setFollowing(true);
        const std::uint64_t memoryBefore = residentBytes();

        // UI stall probe: a 10 ms timer; the largest gap between ticks is the longest block.
        QElapsedTimer sinceTick;
        sinceTick.start();
        qint64 worstGapMs = 0;
        std::uint64_t ticks = 0;
        QTimer probe;
        probe.setTimerType(Qt::PreciseTimer);
        QObject::connect(&probe, &QTimer::timeout, [&] {
            worstGapMs = std::max(worstGapMs, sinceTick.restart());
            ++ticks;
        });
        probe.start(10);

        lv::loggen::WriteOptions options;
        options.out = path;
        options.appendRateBytesPerSecond = rateMb * lv::loggen::kMiB;
        options.durationMs = seconds * 1000;
        std::atomic<bool> finished = false;
        lv::loggen::WriteResult written;
        std::jthread writer([&] {
            written = lv::loggen::writeLog(options);
            finished = true;
        });
        std::uint64_t memoryPeak = memoryBefore;
        QElapsedTimer total;
        total.start();
        while (!finished) {
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            memoryPeak = std::max(memoryPeak, residentBytes());
        }
        writer.join();
        QVERIFY2(written.ok(), written.error.c_str());
        const qint64 writeMs = total.elapsed();

        // Catch up: everything written must be indexed and filtered.
        const auto fileSize = std::filesystem::file_size(path);
        QElapsedTimer catchUp;
        catchUp.start();
        // Indexed, filtered, and the view followed to the last line (its signal is queued).
        const auto caughtUp = [&] {
            const auto content = tab.document().content();
            const auto rows = tab.mainView()->rowCount().get();
            return content && content->index.snapshot().indexedEnd.get() == fileSize &&
                   tab.filter().state() == lv::model::FilteredDocument::State::kDone &&
                   rows == tab.document().lineCount().get() &&
                   tab.mainView()->currentRow() == lv::core::LineNumber(rows - 1);
        };
        while (!caughtUp() && catchUp.elapsed() < 60000) {
            QApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        probe.stop();
        const qint64 catchUpMs = catchUp.elapsed();
        QVERIFY2(caughtUp(), "indexing or filtering did not catch up within 60 s");

        const auto lines = tab.document().lineCount().get();
        const auto indexBytes = tab.document().content()->index.memoryBytes();
        const auto matchBytes = tab.filter().matches()->memoryBytes();
        const double growthMb =
            static_cast<double>(memoryPeak - std::min(memoryPeak, memoryBefore)) /
            (1024.0 * 1024.0);
        qInfo("stress: %llu MiB/s for %llu s, %.1f MiB written, %llu lines, %llu matches",
              static_cast<unsigned long long>(rateMb), static_cast<unsigned long long>(seconds),
              static_cast<double>(written.bytes) / (1024.0 * 1024.0),
              static_cast<unsigned long long>(lines),
              static_cast<unsigned long long>(tab.filter().matchCount()));
        qInfo("stress: write %lld ms, catch-up %lld ms, worst UI stall %lld ms (%llu ticks)",
              writeMs, catchUpMs, worstGapMs, static_cast<unsigned long long>(ticks));
        qInfo("stress: memory before %.1f MiB, peak growth %.1f MiB; index %.2f MiB, matches "
              "%.2f MiB",
              static_cast<double>(memoryBefore) / (1024.0 * 1024.0), growthMb,
              static_cast<double>(indexBytes) / (1024.0 * 1024.0),
              static_cast<double>(matchBytes) / (1024.0 * 1024.0));

        QVERIFY(tab.mainView()->isFollowing());
        QCOMPARE(tab.mainView()->currentRow().value_or(lv::core::LineNumber(0)).get(), lines - 1);
        // P-UI-2: no single UI block above 50 ms (slack for shared CI runners: 150 ms).
        QVERIFY2(worstGapMs <= 150, qPrintable(QStringLiteral("UI stall %1 ms").arg(worstGapMs)));
        // P-TAIL-2: memory bounded (index + match list + caches), not proportional to the data.
        const double writtenMb = static_cast<double>(written.bytes) / (1024.0 * 1024.0);
        QVERIFY2(growthMb < 200.0 && growthMb < writtenMb / 4.0,
                 qPrintable(QStringLiteral("memory grew by %1 MiB").arg(growthMb)));
    }
};

QTEST_MAIN(LiveStressTest)
#include "live_stress_test.moc"
