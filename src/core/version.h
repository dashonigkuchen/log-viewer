#pragma once

#include <string_view>

namespace lv::core
{

// Returns the application version as "MAJOR.MINOR.PATCH", taken from the CMake project version.
// Thread-safety: safe to call from any thread.
[[nodiscard]] std::string_view versionString() noexcept;

}  // namespace lv::core
