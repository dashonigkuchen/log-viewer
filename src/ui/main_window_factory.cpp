#include "ui/main_window_factory.h"

#include "model/model_info.h"

#include <QMainWindow>

namespace lv::ui
{

std::unique_ptr<QWidget> createMainWindow()
{
    auto window = std::make_unique<QMainWindow>();
    window->setWindowTitle(QStringLiteral("Log Viewer %1").arg(lv::model::versionString()));
    window->resize(1024, 768);
    return window;
}

}  // namespace lv::ui
