#include "core/version.h"
#include "lv/core/version_config.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE("versionString matches the CMake project version", "[core][version]")
{
    CHECK(lv::core::versionString() == std::string_view{LV_VERSION_STRING});
}

TEST_CASE("versionString has the form MAJOR.MINOR.PATCH", "[core][version]")
{
    const auto version = lv::core::versionString();
    std::size_t dots = 0;
    for (const char c : version) {
        if (c == '.') {
            ++dots;
        } else {
            CHECK((c >= '0' && c <= '9'));
        }
    }
    CHECK(dots == 2);
    CHECK_FALSE(version.empty());
}
