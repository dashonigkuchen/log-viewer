#pragma once

#include <QWidget>

#include <memory>

namespace lv::ui
{

// Creates the top-level window. Placeholder until MainWindow arrives in M0-05.
[[nodiscard]] std::unique_ptr<QWidget> createMainWindow();

}  // namespace lv::ui
