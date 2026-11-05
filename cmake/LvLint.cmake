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
    # Generated headers (moc, version_config.h) must exist before clang-tidy runs, so tidy
    # depends on every executable of the project. They are collected from the directory tree,
    # so new test executables need no change here.
    function(_lv_collect_executables directory out)
        get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        set(_found "")
        foreach(_target IN LISTS _targets)
            get_target_property(_type ${_target} TYPE)
            if(_type STREQUAL "EXECUTABLE")
                list(APPEND _found ${_target})
            endif()
        endforeach()
        get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        foreach(_subdirectory IN LISTS _subdirectories)
            _lv_collect_executables("${_subdirectory}" _nested)
            list(APPEND _found ${_nested})
        endforeach()
        set(${out} ${_found} PARENT_SCOPE)
    endfunction()
    _lv_collect_executables("${PROJECT_SOURCE_DIR}" _lv_executables)
    if(_lv_executables)
        add_dependencies(tidy ${_lv_executables})
    endif()
else()
    message(STATUS "LogViewer: clang-tidy not found, tidy target disabled.")
endif()
