#include "model/settings.h"

#include "core/util/log.h"

#include <QDir>
#include <QFile>
#include <QSettings>

#include <algorithm>

namespace lv::model
{

namespace
{

constexpr int kMaxTabWidth = 32;

QString key(const char* name)
{
    return QString::fromLatin1(name);
}

int readInt(const QSettings& store, const char* name, int fallback, int min, int max)
{
    bool ok = false;
    const int value = store.value(key(name)).toInt(&ok);
    return ok && value >= min && value <= max ? value : fallback;
}

std::optional<std::uint32_t> readColor(const QSettings& store, const char* name)
{
    const QString text = store.value(key(name)).toString();
    bool ok = false;
    const uint value = text.mid(1).toUInt(&ok, 16);
    if (!text.startsWith(u'#') || !ok || text.size() != 9) {
        return std::nullopt;
    }
    return value;
}

QString colorText(std::uint32_t color)
{
    return QStringLiteral("#%1").arg(color, 8, 16, QChar(u'0')).toUpper();
}

}  // namespace

Settings::Settings(const QString& directory, QObject* parent)
    : QObject(parent)
{
    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(QStringLiteral("logviewer.ini"));
    m_store = std::make_unique<QSettings>(path, QSettings::IniFormat);
    if (m_store->status() == QSettings::FormatError) {
        // F-SET-3: keep the broken file for the user and start with defaults.
        m_store.reset();
        const QString backup = path + QStringLiteral(".corrupt");
        QFile::remove(backup);
        QFile::rename(path, backup);
        core::log(core::LogLevel::kWarning,
                  "settings file was corrupt and has been reset: " + backup.toStdString());
        m_corruptBackup = backup;
        m_store = std::make_unique<QSettings>(path, QSettings::IniFormat);
    }
    load();
}

Settings::~Settings() = default;

QString Settings::filePath() const
{
    return m_store->fileName();
}

void Settings::load()
{
    // F-SET-3: migrate older files; newer ones are read as far as the keys are known.
    const int version = m_store->value(key("General/schemaVersion"), 0).toInt();
    if (version < kSchemaVersion) {
        migrate(version);
        m_store->setValue(key("General/schemaVersion"), kSchemaVersion);
    }
    m_fontFamily = m_store->value(key("View/fontFamily")).toString();
    m_fontSize = readInt(*m_store, "View/fontSize", kDefaultFontSize, kMinFontSize, kMaxFontSize);
    m_zoom =
        readInt(*m_store, "View/zoom", 0, kMinFontSize - m_fontSize, kMaxFontSize - m_fontSize);
    m_tabWidth = readInt(*m_store, "View/tabWidth", kDefaultTabWidth, 1, kMaxTabWidth);
    m_lineNumbers = m_store->value(key("View/lineNumbers"), true).toBool();
    m_showMarksInFilter = m_store->value(key("View/showMarksInFilter"), true).toBool();
    m_currentLineColor = readColor(*m_store, "Colors/currentLine");
    m_markColor = readColor(*m_store, "Colors/mark").value_or(kDefaultMarkColor);
    m_matchColor = readColor(*m_store, "Colors/match").value_or(kDefaultMatchColor);
    m_pollInterval =
        readInt(*m_store, "Files/pollInterval", kDefaultPollMs, kMinPollMs, kMaxPollMs);
    m_maxColumns =
        readInt(*m_store, "View/maxColumns", kDefaultMaxColumns, kMinMaxColumns, kMaxMaxColumns);
    m_historySize =
        readInt(*m_store, "Filter/historySize", kDefaultHistorySize, 1, kMaxHistorySize);
    m_restoreSession = m_store->value(key("General/restoreSession"), true).toBool();
    m_singleInstance = m_store->value(key("General/singleInstance"), true).toBool();
    m_searchThreads = readInt(*m_store, "Search/threads", 0, 0, kMaxSearchThreads);
}

void Settings::migrate(int fromVersion)
{
    if (fromVersion == 1) {
        // Version 1 kept the polling interval under View/ (never released; example migration).
        const QString old = key("View/pollInterval");
        if (m_store->contains(old)) {
            m_store->setValue(key("Files/pollInterval"), m_store->value(old));
            m_store->remove(old);
        }
    }
}

void Settings::setColorValue(const char* name, std::uint32_t& member, std::uint32_t color)
{
    if (color == member) {
        return;
    }
    member = color;
    m_store->setValue(key(name), colorText(color));
    emit viewSettingsChanged();
}

void Settings::setIntValue(const char* name, int& member, int value, int min, int max)
{
    const int clamped = std::clamp(value, min, max);
    if (clamped == member) {
        return;
    }
    member = clamped;
    m_store->setValue(key(name), clamped);
    emit settingsChanged();
}

void Settings::setBoolValue(const char* name, bool& member, bool value)
{
    if (value == member) {
        return;
    }
    member = value;
    m_store->setValue(key(name), value);
    emit settingsChanged();
}

void Settings::setCurrentLineColor(std::optional<std::uint32_t> color)
{
    if (color == m_currentLineColor) {
        return;
    }
    m_currentLineColor = color;
    if (color) {
        m_store->setValue(key("Colors/currentLine"), colorText(*color));
    } else {
        m_store->remove(key("Colors/currentLine"));
    }
    emit viewSettingsChanged();
}

void Settings::setMarkColor(std::uint32_t color)
{
    setColorValue("Colors/mark", m_markColor, color);
}

void Settings::setMatchColor(std::uint32_t color)
{
    setColorValue("Colors/match", m_matchColor, color);
}

void Settings::setPollInterval(int milliseconds)
{
    setIntValue("Files/pollInterval", m_pollInterval, milliseconds, kMinPollMs, kMaxPollMs);
}

void Settings::setMaxColumns(int columns)
{
    const int before = m_maxColumns;
    setIntValue("View/maxColumns", m_maxColumns, columns, kMinMaxColumns, kMaxMaxColumns);
    if (m_maxColumns != before) {
        emit viewSettingsChanged();
    }
}

void Settings::setFilterHistorySize(int size)
{
    setIntValue("Filter/historySize", m_historySize, size, 1, kMaxHistorySize);
}

void Settings::setRestoreSession(bool restore)
{
    setBoolValue("General/restoreSession", m_restoreSession, restore);
}

void Settings::setSingleInstance(bool single)
{
    setBoolValue("General/singleInstance", m_singleInstance, single);
}

void Settings::setSearchThreads(int threads)
{
    setIntValue("Search/threads", m_searchThreads, threads, 0, kMaxSearchThreads);
}

void Settings::setFont(const QString& family, int size)
{
    m_fontFamily = family;
    m_fontSize = std::clamp(size, kMinFontSize, kMaxFontSize);
    m_zoom = std::clamp(m_zoom, kMinFontSize - m_fontSize, kMaxFontSize - m_fontSize);
    m_store->setValue(key("View/fontFamily"), m_fontFamily);
    m_store->setValue(key("View/fontSize"), m_fontSize);
    m_store->setValue(key("View/zoom"), m_zoom);
    emit viewSettingsChanged();
}

void Settings::setZoom(int zoom)
{
    const int clamped = std::clamp(zoom, kMinFontSize - m_fontSize, kMaxFontSize - m_fontSize);
    if (clamped == m_zoom) {
        return;
    }
    m_zoom = clamped;
    m_store->setValue(key("View/zoom"), m_zoom);
    emit viewSettingsChanged();
}

void Settings::setTabWidth(int width)
{
    const int clamped = std::clamp(width, 1, kMaxTabWidth);
    if (clamped == m_tabWidth) {
        return;
    }
    m_tabWidth = clamped;
    m_store->setValue(key("View/tabWidth"), m_tabWidth);
    emit viewSettingsChanged();
}

void Settings::setLineNumbersVisible(bool visible)
{
    if (visible == m_lineNumbers) {
        return;
    }
    m_lineNumbers = visible;
    m_store->setValue(key("View/lineNumbers"), m_lineNumbers);
    emit viewSettingsChanged();
}

void Settings::setShowMarksInFilter(bool show)
{
    if (show == m_showMarksInFilter) {
        return;
    }
    m_showMarksInFilter = show;
    m_store->setValue(key("View/showMarksInFilter"), m_showMarksInFilter);
    emit viewSettingsChanged();
}

QByteArray Settings::filterSplitterState() const
{
    return m_store->value(key("View/filterSplitter")).toByteArray();
}

void Settings::setFilterSplitterState(const QByteArray& state)
{
    m_store->setValue(key("View/filterSplitter"), state);
}

void Settings::sync()
{
    m_store->sync();
}

}  // namespace lv::model
