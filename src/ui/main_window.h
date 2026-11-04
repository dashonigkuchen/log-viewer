#pragma once

#include <QMainWindow>
#include <QString>

class QTabWidget;

namespace lv::ui
{

// The top-level window: tabs for documents, menus, and a status bar.
// Thread-safety: UI thread only.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    [[nodiscard]] QTabWidget* tabWidget() const noexcept { return m_tabs; }

    // Rich text shown in the About dialog: version, license, Qt version, AI note.
    [[nodiscard]] static QString aboutText();

private:
    void createMenus();
    void showAbout();

    QTabWidget* m_tabs = nullptr;
};

}  // namespace lv::ui
