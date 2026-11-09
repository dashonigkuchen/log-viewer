#include "model/filter_history.h"

#include "core/util/assert.h"

#include <QSettings>
#include <QString>

#include <algorithm>

namespace lv::model
{

FilterHistory::FilterHistory(QObject* parent, int limit)
    : QObject(parent)
    , m_limit(limit)
{
    LV_ASSERT(limit > 0);
}

void FilterHistory::add(const core::Query& query)
{
    if (query.text.empty()) {
        return;
    }
    const core::Query normalized = query.normalized();
    std::erase(m_entries, normalized);  // operator== compares normalized queries
    m_entries.insert(m_entries.begin(), normalized);
    if (m_entries.size() > static_cast<std::size_t>(m_limit)) {
        m_entries.resize(static_cast<std::size_t>(m_limit));
    }
    emit changed();
}

void FilterHistory::setLimit(int limit)
{
    LV_ASSERT(limit > 0);
    m_limit = limit;
    if (m_entries.size() > static_cast<std::size_t>(m_limit)) {
        m_entries.resize(static_cast<std::size_t>(m_limit));
        emit changed();
    }
}

void FilterHistory::remove(std::size_t index)
{
    if (index < m_entries.size()) {
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
        emit changed();
    }
}

void FilterHistory::clear()
{
    if (!m_entries.empty()) {
        m_entries.clear();
        emit changed();
    }
}

void FilterHistory::load(QSettings& settings)
{
    m_entries.clear();
    const int size = settings.beginReadArray(QStringLiteral("FilterHistory"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        core::Query query;
        query.text = settings.value(QStringLiteral("text")).toString().toStdString();
        const int mode = settings.value(QStringLiteral("mode"), 0).toInt();
        if (mode < 0 || mode > static_cast<int>(core::QueryMode::kBoolean) || query.text.empty()) {
            continue;  // unknown or broken entry
        }
        query.mode = static_cast<core::QueryMode>(mode);
        query.caseSensitive = settings.value(QStringLiteral("caseSensitive"), false).toBool();
        query.wholeWord = settings.value(QStringLiteral("wholeWord"), false).toBool();
        query.invert = settings.value(QStringLiteral("invert"), false).toBool();
        query = query.normalized();
        if (std::ranges::find(m_entries, query) == m_entries.end() &&
            m_entries.size() < static_cast<std::size_t>(m_limit)) {
            m_entries.push_back(query);
        }
    }
    settings.endArray();
    emit changed();
}

void FilterHistory::save(QSettings& settings) const
{
    settings.remove(QStringLiteral("FilterHistory"));
    settings.beginWriteArray(QStringLiteral("FilterHistory"), static_cast<int>(m_entries.size()));
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const auto& query = m_entries[i];
        settings.setArrayIndex(static_cast<int>(i));
        settings.setValue(QStringLiteral("text"), QString::fromStdString(query.text));
        settings.setValue(QStringLiteral("mode"), static_cast<int>(query.mode));
        settings.setValue(QStringLiteral("caseSensitive"), query.caseSensitive);
        settings.setValue(QStringLiteral("wholeWord"), query.wholeWord);
        settings.setValue(QStringLiteral("invert"), query.invert);
    }
    settings.endArray();
}

}  // namespace lv::model
