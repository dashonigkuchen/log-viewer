# Targets: format, format-check, tidy (see docs/DEVELOPMENT.md §5 and tools/lint.py).

find_package(Python3 3.11 COMPONENTS Interpreter)
find_program(LV_CLANG_FORMAT NAMES clang-format-19 clang-format)
find_program(LV_CLANG_TIDY NAMES clang-tidy-19 clang-tidy)

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "LogViewer: Python 3.11+ not found, lint targets disabled.")
    return()
endif()

set(_lv_lint "${PROJECT_SOURCE_DIR}/tools/lint.py")

if(LV_CLANG_FORMAT)
    add_custom_target(
        format
        COMMAND "${Python3_EXECUTABLE}" -I "${_lv_lint}" format --clang-format "${LV_CLANG_FORMAT}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Formatting sources with clang-format"
        VERBATIM USES_TERMINAL
    )
    add_custom_target(
        format-check
        COMMAND "${Python3_EXECUTABLE}" -I "${_lv_lint}" format-check --clang-format
                "${LV_CLANG_FORMAT}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Checking formatting with clang-format"
        VERBATIM USES_TERMINAL
    )
else()
    message(STATUS "LogViewer: clang-format not found, format targets disabled.")
endif()

if(LV_CLANG_TIDY)
    add_custom_target(
        tidy
        COMMAND "${Python3_EXECUTABLE}" -I "${_lv_lint}" tidy --clang-tidy "${LV_CLANG_TIDY}"
                --build-dir "${PROJECT_BINARY_DIR}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Running clang-tidy"
        VERBATIM USES_TERMINAL
    )
    # Generated headers (moc, version_config.h) must exist before clang-tidy runs.
    add_dependencies(tidy lv_app)
    foreach(_lv_test_target IN ITEMS lv_core_tests lv_model_tests lv_ui_tests lv_tools_tests lv_bench)
        if(TARGET ${_lv_test_target})
            add_dependencies(tidy ${_lv_test_target})
        endif()
    endforeach()
else()
    message(STATUS "LogViewer: clang-tidy not found, tidy target disabled.")
endif()
