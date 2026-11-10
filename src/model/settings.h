#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>

class QSettings;

namespace lv::model
{

// Light / dark appearance (SPEC F-SET-4); kSystem follows the operating system.
enum class ColorScheme : std::uint8_t
{
    kSystem,
    kLight,
    kDark,
};

// Typed access to the INI settings file (SPEC F-SET-2, F-SET-3, ARCHITECTURE §5). Values are
// read once, cached, and written immediately when changed. Unknown keys are ignored; values out
// of range fall back to their defaults; a corrupt file is renamed to *.corrupt and replaced.
// QtCore only: the font is stored as family and size, the UI builds the QFont.
//
// Thread-safety: UI thread only.
class Settings : public QObject
{
    Q_OBJECT

public:
    // Version 2 added the settings dialog values (M6-01), version 3 theme-dependent color
    // defaults (M6-02); older files are migrated.
    static constexpr int kSchemaVersion = 3;
    static constexpr int kDefaultTabWidth = 8;
    static constexpr int kDefaultFontSize = 10;
    static constexpr int kMinFontSize = 4;
    static constexpr int kMaxFontSize = 72;
    static constexpr int kDefaultPollMs = 1000;
    static constexpr int kMinPollMs = 100;
    static constexpr int kMaxPollMs = 10000;
    static constexpr int kDefaultMaxColumns = 100000;
    static constexpr int kMinMaxColumns = 100;
    static constexpr int kMaxMaxColumns = 1000000;
    static constexpr int kDefaultHistorySize = 100;
    static constexpr int kMaxHistorySize = 1000;
    static constexpr int kMaxSearchThreads = 64;
    // Colors are 0xAARRGGBB (QtCore only); an unset color uses the light or dark default of the
    // log view (F-SET-4).

    // Opens `<directory>/logviewer.ini` (the directory is created if needed).
    explicit Settings(const QString& directory, QObject* parent = nullptr);
    ~Settings() override;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;
    Settings(Settings&&) = delete;
    Settings& operator=(Settings&&) = delete;

    [[nodiscard]] QString filePath() const;

    // The underlying store, for components that keep their own groups (RecentFiles).
    [[nodiscard]] QSettings& store() const noexcept { return *m_store; }

    // Font of log views (F-VIEW-3). An empty family means the system fixed-pitch font.
    [[nodiscard]] const QString& fontFamily() const noexcept { return m_fontFamily; }

    [[nodiscard]] int fontSize() const noexcept { return m_fontSize; }

    void setFont(const QString& family, int size);

    // Zoom in points relative to the font size (Ctrl+Plus / Ctrl+Minus / Ctrl+0).
    [[nodiscard]] int zoom() const noexcept { return m_zoom; }

    void setZoom(int zoom);

    // Font size with the zoom applied, within [kMinFontSize, kMaxFontSize].
    [[nodiscard]] int effectiveFontSize() const noexcept { return m_fontSize + m_zoom; }

    [[nodiscard]] int tabWidth() const noexcept { return m_tabWidth; }

    void setTabWidth(int width);

    [[nodiscard]] bool lineNumbersVisible() const noexcept { return m_lineNumbers; }

    void setLineNumbersVisible(bool visible);

    // Marked lines also appear in the filtered view (F-FILT-9).
    [[nodiscard]] bool showMarksInFilter() const noexcept { return m_showMarksInFilter; }

    void setShowMarksInFilter(bool show);

    // Settings dialog values (F-SET-1).
    // Current line background; nullopt = derived from the palette.
    [[nodiscard]] std::optional<std::uint32_t> currentLineColor() const noexcept
    {
        return m_currentLineColor;
    }

    void setCurrentLineColor(std::optional<std::uint32_t> color);

    [[nodiscard]] std::optional<std::uint32_t> markColor() const noexcept { return m_markColor; }

    void setMarkColor(std::optional<std::uint32_t> color);

    [[nodiscard]] std::optional<std::uint32_t> matchColor() const noexcept { return m_matchColor; }

    void setMatchColor(std::optional<std::uint32_t> color);

    [[nodiscard]] ColorScheme colorScheme() const noexcept { return m_colorScheme; }

    void setColorScheme(ColorScheme scheme);

    // File change polling interval in ms (F-TAIL-2).
    [[nodiscard]] int pollInterval() const noexcept { return m_pollInterval; }

    void setPollInterval(int milliseconds);

    // Displayed characters per line before it is cut (F-FILE-7).
    [[nodiscard]] int maxColumns() const noexcept { return m_maxColumns; }

    void setMaxColumns(int columns);

    [[nodiscard]] int filterHistorySize() const noexcept { return m_historySize; }

    void setFilterHistorySize(int size);

    // Used by M6-03 / M6-04; stored here so the dialog can show them.
    [[nodiscard]] bool restoreSession() const noexcept { return m_restoreSession; }

    void setRestoreSession(bool restore);

    [[nodiscard]] bool singleInstance() const noexcept { return m_singleInstance; }

    void setSingleInstance(bool single);

    // Search threads; 0 = automatic. Takes effect at the next start (the pool is global).
    [[nodiscard]] int searchThreads() const noexcept { return m_searchThreads; }

    void setSearchThreads(int threads);

    // Path of the backup if the settings file was corrupt and has been reset (F-SET-3).
    [[nodiscard]] const QString& corruptBackup() const noexcept { return m_corruptBackup; }

    // Saved QSplitter state between main and filtered view (F-FILT-1); empty if never saved.
    [[nodiscard]] QByteArray filterSplitterState() const;
    void setFilterSplitterState(const QByteArray& state);

    // Writes pending changes to disk.
    void sync();

signals:
    // A view setting changed (font, zoom, tab width, line numbers).
    void viewSettingsChanged();
    // Any other setting changed (polling, history size, ...).
    void settingsChanged();

private:
    void load();
    void migrate(int fromVersion);
    void setColorValue(const char* name, std::optional<std::uint32_t>& member,
                       std::optional<std::uint32_t> color);
    void setIntValue(const char* name, int& member, int value, int min, int max);
    void setBoolValue(const char* name, bool& member, bool value);

    std::unique_ptr<QSettings> m_store;
    QString m_fontFamily;
    int m_fontSize = kDefaultFontSize;
    int m_zoom = 0;
    int m_tabWidth = kDefaultTabWidth;
    bool m_lineNumbers = true;
    bool m_showMarksInFilter = true;
    std::optional<std::uint32_t> m_currentLineColor;
    std::optional<std::uint32_t> m_markColor;
    std::optional<std::uint32_t> m_matchColor;
    ColorScheme m_colorScheme = ColorScheme::kSystem;
    int m_pollInterval = kDefaultPollMs;
    int m_maxColumns = kDefaultMaxColumns;
    int m_historySize = kDefaultHistorySize;
    bool m_restoreSession = true;
    bool m_singleInstance = true;
    int m_searchThreads = 0;
    QString m_corruptBackup;
};

}  // namespace lv::model
