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
    // Schema version 1 is the first one: there is nothing to migrate yet.
    m_store->setValue(key("General/schemaVersion"), kSchemaVersion);
    m_fontFamily = m_store->value(key("View/fontFamily")).toString();
    m_fontSize = readInt(*m_store, "View/fontSize", kDefaultFontSize, kMinFontSize, kMaxFontSize);
    m_zoom =
        readInt(*m_store, "View/zoom", 0, kMinFontSize - m_fontSize, kMaxFontSize - m_fontSize);
    m_tabWidth = readInt(*m_store, "View/tabWidth", kDefaultTabWidth, 1, kMaxTabWidth);
    m_lineNumbers = m_store->value(key("View/lineNumbers"), true).toBool();
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
