#pragma once

#include <QString>

namespace lv::model
{

// Returns the application version as a QString (bridge from lv_core).
// Placeholder until the real model classes arrive in M2.
[[nodiscard]] QString versionString();

}  // namespace lv::model
