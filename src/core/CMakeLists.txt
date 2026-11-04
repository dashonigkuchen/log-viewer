configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version_config.h.in"
    "${PROJECT_BINARY_DIR}/generated/lv/core/version_config.h"
    @ONLY
)

find_package(tl-expected CONFIG REQUIRED)

add_library(
    lv_core STATIC
    version.cpp
    util/assert.cpp
    util/log.cpp
    util/result.cpp
    util/strong_types.cpp
)
add_library(lv::core ALIAS lv_core)

target_include_directories(
    lv_core PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_BINARY_DIR}/generated"
)
target_link_libraries(
    lv_core
    PUBLIC tl::expected
    PRIVATE lv::compile_options
)
