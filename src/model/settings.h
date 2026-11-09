#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QSettings;

namespace lv::model
{

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
    static constexpr int kSchemaVersion = 1;
    static constexpr int kDefaultTabWidth = 8;
    static constexpr int kDefaultFontSize = 10;
    static constexpr int kMinFontSize = 4;
    static constexpr int kMaxFontSize = 72;

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

    // Saved QSplitter state between main and filtered view (F-FILT-1); empty if never saved.
    [[nodiscard]] QByteArray filterSplitterState() const;
    void setFilterSplitterState(const QByteArray& state);

    // Writes pending changes to disk.
    void sync();

signals:
    // A view setting changed (font, zoom, tab width, line numbers).
    void viewSettingsChanged();

private:
    void load();

    std::unique_ptr<QSettings> m_store;
    QString m_fontFamily;
    int m_fontSize = kDefaultFontSize;
    int m_zoom = 0;
    int m_tabWidth = kDefaultTabWidth;
    bool m_lineNumbers = true;
    bool m_showMarksInFilter = true;
};

}  // namespace lv::model
