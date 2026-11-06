#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QSettings;

namespace lv::model
{

// Most recently opened files, newest first (SPEC F-FILE-9). Paths are compared after
// normalization (absolute, clean, case-insensitive on Windows), so each file appears once.
//
// Thread-safety: UI thread only.
class RecentFiles : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultLimit = 20;

    explicit RecentFiles(QObject* parent = nullptr, int limit = kDefaultLimit);

    [[nodiscard]] const QStringList& paths() const noexcept { return m_paths; }

    [[nodiscard]] int limit() const noexcept { return m_limit; }

    // Moves `path` to the front (adding it if new) and drops entries beyond the limit.
    void add(const QString& path);
    void remove(const QString& path);
    // Removes every entry whose file no longer exists.
    void removeMissing();
    void clear();

    // Reads/writes the list under the group "RecentFiles".
    void load(QSettings& settings);
    void save(QSettings& settings) const;

    // Normalized form used for comparison and storage.
    [[nodiscard]] static QString normalize(const QString& path);

signals:
    void changed();

private:
    [[nodiscard]] qsizetype indexOf(const QString& normalized) const;

    QStringList m_paths;
    int m_limit;
};

}  // namespace lv::model
