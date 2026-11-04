# Project-wide compiler options, exposed as the interface target lv_compile_options.
# Every first-party target links it. Never set warning flags per target.

add_library(lv_compile_options INTERFACE)
add_library(lv::compile_options ALIAS lv_compile_options)

target_compile_features(lv_compile_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(
        lv_compile_options
        INTERFACE /W4
                  /permissive-
                  /utf-8
                  /Zc:__cplusplus
                  /Zc:preprocessor
                  /EHsc
                  /external:anglebrackets
                  /external:W0
    )
    if(LV_WARNINGS_AS_ERRORS)
        target_compile_options(lv_compile_options INTERFACE /WX)
    endif()
    target_compile_definitions(
        lv_compile_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00
    )
else()
    target_compile_options(
        lv_compile_options
        INTERFACE -Wall
                  -Wextra
                  -Wpedantic
                  -Wconversion
                  -Wsign-conversion
                  -Wshadow
                  -Wnon-virtual-dtor
                  -Wold-style-cast
                  -Wcast-align
                  -Wnull-dereference
                  -Wdouble-promotion
                  -Wformat=2
                  -Wimplicit-fallthrough
    )
    if(LV_WARNINGS_AS_ERRORS)
        target_compile_options(lv_compile_options INTERFACE -Werror)
    endif()
    # Release builds target generic x86-64-v2 (SSE4.2, POPCNT). Never -march=native.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        target_compile_options(lv_compile_options INTERFACE -march=x86-64-v2)
    endif()
endif()
