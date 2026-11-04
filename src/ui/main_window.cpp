#include "ui/main_window.h"

#include "model/model_info.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QtVersion>

namespace lv::ui
{

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_tabs(new QTabWidget(this))
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Log Viewer %1").arg(lv::model::versionString()));
    resize(1024, 768);

    m_tabs->setObjectName(QStringLiteral("documentTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    setCentralWidget(m_tabs);

    createMenus();
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->setObjectName(QStringLiteral("menuFile"));

    QAction* openAction = fileMenu->addAction(tr("&Open..."));
    openAction->setObjectName(QStringLiteral("actionOpen"));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setEnabled(false);  // Opening files arrives in roadmap step M2-06.

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setObjectName(QStringLiteral("actionExit"));
    exitAction->setShortcut(QKeySequence::Quit);
    exitAction->setMenuRole(QAction::QuitRole);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->setObjectName(QStringLiteral("menuHelp"));

    QAction* aboutAction = helpMenu->addAction(tr("&About Log Viewer"));
    aboutAction->setObjectName(QStringLiteral("actionAbout"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

QString MainWindow::aboutText()
{
    return tr("<h3>Log Viewer %1</h3>"
              "<p>A fast native viewer for large log files.</p>"
              "<p>Licensed under the MIT License. Qt is used under the GNU LGPL v3.</p>"
              "<p>Built with Qt %2, running on Qt %3.</p>"
              "<p>Developed with AI coding agents.</p>")
        .arg(lv::model::versionString(), QStringLiteral(QT_VERSION_STR),
             QString::fromLatin1(qVersion()));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Log Viewer"), aboutText());
}

}  // namespace lv::ui
