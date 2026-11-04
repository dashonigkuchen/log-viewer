#include "core/version.h"

#include "lv/core/version_config.h"

namespace lv::core
{

std::string_view versionString() noexcept
{
    return LV_VERSION_STRING;
}

}  // namespace lv::core
