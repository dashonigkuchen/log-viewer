configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version_config.h.in"
    "${PROJECT_BINARY_DIR}/generated/lv/core/version_config.h"
    @ONLY
)

find_package(tl-expected CONFIG REQUIRED)
find_package(simdutf CONFIG REQUIRED)

add_library(
    lv_core STATIC
    version.cpp
    encoding/encoding.cpp
    index/line_splitter.cpp
    index/sorted_u64_sequence.cpp
    platform/file_common.cpp
    $<IF:$<BOOL:${WIN32}>,platform/file_win32.cpp,platform/file_posix.cpp>
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
    PRIVATE lv::compile_options simdutf::simdutf
)
