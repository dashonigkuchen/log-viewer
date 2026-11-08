#pragma once

#include "core/encoding/encoding.h"
#include "core/index/line_index.h"
#include "core/platform/file.h"
#include "core/reader/line_reader.h"
#include "core/tail/change_detector.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace lv::model
{

class FileWatcher;

// The opened file of one generation with its line index and reader. The indexer thread is the
// single writer of `index`; everything else only reads (snapshots are thread-safe).
// Shared by the document and the views; a reload creates a new instance.
struct DocumentContent
{
    DocumentContent(core::File openedFile, core::Encoding encoding, std::uint64_t dataStart,
                    std::uint64_t contentGeneration);

    core::File file;
    core::LineIndex index;
    core::LineReader reader;
    std::uint64_t generation;
    // What the indexed part looked like (size = indexed end); compared with the file to detect
    // changes. Worker-only: workers of a document run one after another.
    core::FileSnapshot watched;
};

// One opened log file (ARCHITECTURE §5): opens and indexes it on a worker thread, publishes
// progress, and supports reload and an encoding override (SPEC F-FILE-8, F-ENC-3, F-FILE-12).
//
// Every (re)start increments generation(); signals of an older generation are dropped, so
// listeners never see stale data.
//
// Once indexed, the file is watched (FileWatcher, F-TAIL-2). On a change a worker classifies it
// (core::ChangeDetector): growth is indexed incrementally from the last complete line, so a
// partial last line is completed and never counted twice (F-TAIL-1, F-TAIL-8), followed by
// linesAppended(). Truncation and replacement reload the document (notices, marks, and the
// missing state follow in M4-06); a missing path keeps the content.
//
// Thread-safety: UI thread only (signals are emitted on the thread that owns the object).
class Document : public QObject
{
    Q_OBJECT

public:
    enum class State : std::uint8_t
    {
        kClosed,    // not opened yet, or closed
        kIndexing,  // opening or indexing; content() may already show a prefix
        kReady,     // the whole file is indexed
        kError,     // open or read failed; see errorMessage()
    };
    Q_ENUM(State)

    explicit Document(std::filesystem::path path, QObject* parent = nullptr);
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = delete;
    Document& operator=(Document&&) = delete;
    ~Document() override;

    // Opens the file and starts indexing (also restarts a running index).
    void open();

    // Re-indexes from the beginning with a new generation (F-FILE-8).
    void reload();

    // nullopt = detect. Changing it reloads the document (F-ENC-3).
    void setEncodingOverride(std::optional<core::Encoding> encoding);

    // Stops indexing, joins the worker, and releases the file.
    void close();

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

    [[nodiscard]] State state() const noexcept { return m_state; }

    [[nodiscard]] const QString& errorMessage() const noexcept { return m_errorMessage; }

    [[nodiscard]] std::uint64_t generation() const noexcept { return m_generation; }

    [[nodiscard]] std::optional<core::Encoding> encodingOverride() const noexcept
    {
        return m_encodingOverride;
    }

    // Content of the current generation, or null before the file was opened (or after errors).
    [[nodiscard]] std::shared_ptr<const DocumentContent> content() const { return m_content; }

    // Lines visible to readers now (including a partial last line).
    [[nodiscard]] core::LineCount lineCount() const;

    // Watching (F-TAIL-2). Applied now and to the watcher created once the file is indexed.
    void setPollInterval(int milliseconds);
    void setWatchNotificationsEnabled(bool enabled);

    // Null before the file was indexed for the first time.
    [[nodiscard]] FileWatcher* watcher() const noexcept { return m_watcher; }

    // Number of completed incremental index runs (diagnostics, tests).
    [[nodiscard]] std::uint64_t tailRuns() const noexcept { return m_tailRuns; }

signals:
    void stateChanged(lv::model::Document::State state);
    void indexingProgress(quint64 indexedBytes, quint64 totalBytes);
    // The line count grew to `lineCount` (during indexing; later also when the file grows).
    void linesAppended(quint64 lineCount);
    // A new generation started: all lines, marks, and caches of older generations are invalid.
    void reset(quint64 generation);

private:
    // Runs the events queued by the worker (on the UI thread).
    Q_INVOKABLE void drainEvents();

    void startWorker();
    void stopWorker();
    void runWorker(const std::stop_token& stop, std::uint64_t generation,
                   std::optional<core::Encoding> encodingOverride);
    // Checks the file for changes on a worker (or once the current worker is done).
    void scheduleTailCheck();
    void runTail(const std::stop_token& stop, std::uint64_t generation,
                 const std::shared_ptr<DocumentContent>& content);
    void workerDone();
    void ensureWatcher();
    void setState(State state);

    // Worker: queues `function` to run on the UI thread if the generation is still current.
    // Events go through a mutex-protected queue (visible to ThreadSanitizer); only a parameterless
    // wake-up crosses the Qt event loop, coalesced while one is pending.
    void post(std::uint64_t generation, std::function<void()> function);

    struct PendingEvent
    {
        std::uint64_t generation = 0;
        std::function<void()> function;
    };

    std::filesystem::path m_path;
    State m_state = State::kClosed;
    QString m_errorMessage;
    std::uint64_t m_generation = 0;
    std::optional<core::Encoding> m_encodingOverride;
    std::shared_ptr<DocumentContent> m_content;
    std::mutex m_eventMutex;
    std::vector<PendingEvent> m_events;
    bool m_drainScheduled = false;
    FileWatcher* m_watcher = nullptr;  // child; created when first indexed
    int m_pollInterval = 1000;
    bool m_watchNotifications = true;
    bool m_workerBusy = false;  // a worker of the current generation is running
    bool m_recheck = false;     // a change arrived while the worker was busy
    std::uint64_t m_tailRuns = 0;

    std::jthread m_worker;  // last member: joined before the queue is destroyed
};

}  // namespace lv::model
