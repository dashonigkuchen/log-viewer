#pragma once

#include "core/search/query.h"

#include <QObject>

#include <vector>

class QSettings;

namespace lv::model
{

// Global history of applied filters (SPEC F-HIST-1 … F-HIST-4): most recent first,
// deduplicated by (text, mode, options), at most limit() entries, persisted in the settings.
//
// Thread-safety: UI thread only.
class FilterHistory : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultLimit = 100;

    explicit FilterHistory(QObject* parent = nullptr, int limit = kDefaultLimit);

    [[nodiscard]] const std::vector<core::Query>& entries() const noexcept { return m_entries; }

    [[nodiscard]] int limit() const noexcept { return m_limit; }

    // Moves `query` to the front (adding it if new) and drops entries beyond the limit.
    // Empty queries are ignored.
    void add(const core::Query& query);
    void remove(std::size_t index);
    void clear();

    // Reads/writes the array "FilterHistory" (text, mode, caseSensitive, wholeWord, invert).
    void load(QSettings& settings);
    void save(QSettings& settings) const;

signals:
    void changed();

private:
    std::vector<core::Query> m_entries;
    int m_limit;
};

}  // namespace lv::model
