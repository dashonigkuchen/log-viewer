cmake_minimum_required(VERSION 3.28)

project(
    LogViewer
    VERSION 0.1.0
    DESCRIPTION "Fast native log viewer"
    HOMEPAGE_URL "https://github.com/dashonigkuchen/log-viewer"
    LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a preset: cmake --preset linux-gcc-debug")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++20 modules are used; skipping the dependency scan keeps builds fast and ccache-friendly.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(LV_BUILD_TESTS "Build the test suites" ON)
option(LV_BUILD_BENCH "Build the benchmarks (needs the vcpkg 'bench' feature)" OFF)
option(LV_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(LV_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LV_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(LV_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(LvCompilerOptions)
include(LvSanitizers)
include(LvChecks)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

if(LV_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src)

if(LV_BUILD_TESTS)
    add_subdirectory(tests)
endif()

lv_assert_target_has_no_qt(lv_core)

include(LvLint)
