#include "model/model_info.h"

#include "core/version.h"

namespace lv::model
{

QString versionString()
{
    const auto version = lv::core::versionString();
    return QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size()));
}

}  // namespace lv::model
