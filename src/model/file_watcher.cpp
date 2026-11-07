#include "model/file_watcher.h"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimeZone>

#include <algorithm>
#include <utility>

namespace lv::model
{

FileWatcher::FileWatcher(std::filesystem::path path, QObject* parent)
    : QObject(parent)
    , m_path(std::move(path))
    , m_file(QString::fromStdString(m_path.string()))
    , m_directory(QString::fromStdString(m_path.parent_path().string()))
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &FileWatcher::notified);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileWatcher::notified);
    m_poll.setInterval(kDefaultPollMs);
    connect(&m_poll, &QTimer::timeout, this, &FileWatcher::poll);
    m_coalesce.setSingleShot(true);
    connect(&m_coalesce, &QTimer::timeout, this, &FileWatcher::fire);
    m_last = stamp();
    updateOsWatches();
    m_poll.start();
}

FileWatcher::~FileWatcher() = default;

void FileWatcher::setHandleSizeProbe(HandleSizeProbe probe)
{
    m_probe = std::move(probe);
    m_last = stamp();
}

void FileWatcher::setPollInterval(int milliseconds)
{
    m_poll.setInterval(std::clamp(milliseconds, kMinPollMs, kMaxPollMs));
}

void FileWatcher::setNotificationsEnabled(bool enabled)
{
    m_notify = enabled;
    updateOsWatches();
}

void FileWatcher::pause()
{
    m_paused = true;
    m_poll.stop();
    m_coalesce.stop();
    updateOsWatches();
}

void FileWatcher::resume()
{
    if (!m_paused) {
        return;
    }
    m_paused = false;
    m_last = stamp();
    updateOsWatches();
    m_poll.start();
    schedule();  // something may have changed while paused
}

FileWatcher::Stamp FileWatcher::stamp() const
{
    const QFileInfo info(m_file);  // fresh object: no cached data
    Stamp result;
    result.exists = info.exists();
    if (result.exists) {
        result.size = info.size();
        result.modified = info.lastModified(QTimeZone::UTC);
    }
    if (m_probe) {
        result.handleSize = m_probe();
    }
    return result;
}

void FileWatcher::updateOsWatches()
{
    const QStringList watched = m_watcher->files() + m_watcher->directories();
    const bool want = m_notify && !m_paused;
    if (!want) {
        if (!watched.isEmpty()) {
            m_watcher->removePaths(watched);
        }
        return;
    }
    // QFileSystemWatcher drops a file that was removed or renamed; re-add it once it exists.
    if (!m_watcher->files().contains(m_file) && QFileInfo::exists(m_file)) {
        m_watcher->addPath(m_file);
    }
    if (!m_watcher->directories().contains(m_directory) && QFileInfo(m_directory).isDir()) {
        m_watcher->addPath(m_directory);
    }
}

void FileWatcher::notified()
{
    if (m_paused) {
        return;
    }
    updateOsWatches();
    schedule();
}

void FileWatcher::poll()
{
    updateOsWatches();
    const Stamp now = stamp();
    if (now != m_last) {
        m_last = now;
        schedule();
    }
}

void FileWatcher::schedule()
{
    if (m_coalesce.isActive()) {
        return;  // already pending: coalesce
    }
    const qint64 since = m_sinceFire.isValid() ? m_sinceFire.elapsed() : kCoalesceMs;
    m_coalesce.start(static_cast<int>(std::max<qint64>(0, kCoalesceMs - since)));
}

void FileWatcher::fire()
{
    if (m_paused) {
        return;
    }
    m_sinceFire.start();
    m_last = stamp();
    ++m_signals;
    emit changed();
}

}  // namespace lv::model
