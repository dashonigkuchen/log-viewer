#include "model/recent_files.h"

#include "core/util/assert.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace lv::model
{

namespace
{

constexpr auto kGroup = "RecentFiles";
constexpr auto kKey = "paths";

Qt::CaseSensitivity pathCase()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

}  // namespace

RecentFiles::RecentFiles(QObject* parent, int limit)
    : QObject(parent)
    , m_limit(limit)
{
    LV_ASSERT(limit > 0);
}

QString RecentFiles::normalize(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

qsizetype RecentFiles::indexOf(const QString& normalized) const
{
    for (qsizetype i = 0; i < m_paths.size(); ++i) {
        if (m_paths[i].compare(normalized, pathCase()) == 0) {
            return i;
        }
    }
    return -1;
}

void RecentFiles::add(const QString& path)
{
    const QString normalized = normalize(path);
    if (const qsizetype index = indexOf(normalized); index >= 0) {
        m_paths.removeAt(index);
    }
    m_paths.prepend(normalized);
    while (m_paths.size() > m_limit) {
        m_paths.removeLast();
    }
    emit changed();
}

void RecentFiles::remove(const QString& path)
{
    if (const qsizetype index = indexOf(normalize(path)); index >= 0) {
        m_paths.removeAt(index);
        emit changed();
    }
}

void RecentFiles::removeMissing()
{
    const auto removed =
        m_paths.removeIf([](const QString& path) { return !QFileInfo::exists(path); });
    if (removed > 0) {
        emit changed();
    }
}

void RecentFiles::clear()
{
    if (!m_paths.isEmpty()) {
        m_paths.clear();
        emit changed();
    }
}

void RecentFiles::load(QSettings& settings)
{
    settings.beginGroup(QLatin1StringView(kGroup));
    const QStringList stored = settings.value(QLatin1StringView(kKey)).toStringList();
    settings.endGroup();
    m_paths.clear();
    // Keep the stored order (newest first); the first occurrence of a path wins.
    for (const QString& entry : stored) {
        if (entry.isEmpty()) {
            continue;
        }
        const QString normalized = normalize(entry);
        if (indexOf(normalized) < 0) {
            m_paths.append(normalized);
        }
    }
    while (m_paths.size() > m_limit) {
        m_paths.removeLast();
    }
    emit changed();
}

void RecentFiles::save(QSettings& settings) const
{
    settings.beginGroup(QLatin1StringView(kGroup));
    settings.setValue(QLatin1StringView(kKey), m_paths);
    settings.endGroup();
}

}  // namespace lv::model
