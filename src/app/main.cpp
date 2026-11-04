#include "ui/main_window_factory.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    const QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LogViewer"));
    QApplication::setApplicationName(QStringLiteral("LogViewer"));

    const auto window = lv::ui::createMainWindow();
    window->show();
    return QApplication::exec();
}
