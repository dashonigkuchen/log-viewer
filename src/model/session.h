#pragma once

#include "core/search/query.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

class QSettings;

namespace lv::model
{

// What is restored of one tab (SPEC F-FILE-10).
struct SessionTab
{
    QString path;
    std::uint64_t topRow = 0;
    std::optional<std::uint64_t> currentRow;
    std::optional<core::Query> filter;
    bool following = false;

    friend bool operator==(const SessionTab&, const SessionTab&) = default;
};

// Tabs and window layout of the last session (F-FILE-10, F-TAB-5), stored in the settings INI
// under "Session". Window geometry and state are opaque QMainWindow blobs.
struct Session
{
    std::vector<SessionTab> tabs;
    int currentTab = 0;
    QByteArray geometry;
    QByteArray windowState;

    void save(QSettings& settings) const;
    [[nodiscard]] static Session load(QSettings& settings);

    friend bool operator==(const Session&, const Session&) = default;
};

}  // namespace lv::model
