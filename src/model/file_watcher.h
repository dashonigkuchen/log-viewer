#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

class QFileSystemWatcher;

namespace lv::model
{

// Tells a document when its file may have changed (ARCHITECTURE §5, SPEC F-TAIL-2). Two sources
// feed one coalesced changed() signal (at most one per kCoalesceMs):
// - OS notifications: QFileSystemWatcher on the file and on its directory (so that deletion,
//   rotation, and recreation are seen; the file is re-added when it reappears).
// - A polling timer that is always active as a safety net. It compares existence, size, and
//   modification time of the path and, if a probe is set, the size of the open handle (NTFS
//   updates directory entries lazily while a writer keeps the file open).
// The receiver runs core::ChangeDetector to classify the change; spurious signals are harmless.
//
// Thread-safety: UI thread only.
class FileWatcher : public QObject
{
    Q_OBJECT

public:
    static constexpr int kCoalesceMs = 50;
    static constexpr int kDefaultPollMs = 1000;
    static constexpr int kMinPollMs = 100;
    static constexpr int kMaxPollMs = 10000;

    // Size of the open handle, or nullopt if unknown.
    using HandleSizeProbe = std::function<std::optional<std::uint64_t>()>;

    explicit FileWatcher(std::filesystem::path path, QObject* parent = nullptr);
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    FileWatcher(FileWatcher&&) = delete;
    FileWatcher& operator=(FileWatcher&&) = delete;
    ~FileWatcher() override;

    void setHandleSizeProbe(HandleSizeProbe probe);

    // Clamped to [kMinPollMs, kMaxPollMs].
    void setPollInterval(int milliseconds);

    [[nodiscard]] int pollInterval() const noexcept { return m_poll.interval(); }

    // OS notifications on/off (polling stays active); for tests and network drives.
    void setNotificationsEnabled(bool enabled);

    [[nodiscard]] bool notificationsEnabled() const noexcept { return m_notify; }

    // Pauses all watching (F-TAIL-10); resume() emits changed() once.
    void pause();
    void resume();

    [[nodiscard]] bool isPaused() const noexcept { return m_paused; }

    // Number of changed() signals so far (diagnostics, tests).
    [[nodiscard]] std::uint64_t signalCount() const noexcept { return m_signals; }

signals:
    void changed();

private:
    struct Stamp
    {
        bool exists = false;
        qint64 size = -1;
        QDateTime modified;
        std::optional<std::uint64_t> handleSize;

        bool operator==(const Stamp&) const = default;
    };

    [[nodiscard]] Stamp stamp() const;
    void poll();
    void notified();
    void schedule();
    void fire();
    void updateOsWatches();

    std::filesystem::path m_path;
    QString m_file;
    QString m_directory;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer m_poll;
    QTimer m_coalesce;
    QElapsedTimer m_sinceFire;
    HandleSizeProbe m_probe;
    Stamp m_last;
    bool m_notify = true;
    bool m_paused = false;
    std::uint64_t m_signals = 0;
};

}  // namespace lv::model
