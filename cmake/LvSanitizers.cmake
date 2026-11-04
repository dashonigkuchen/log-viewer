# Sanitizer options (GCC/Clang only). Applied through lv_compile_options.

if(LV_ENABLE_TSAN AND (LV_ENABLE_ASAN OR LV_ENABLE_UBSAN))
    message(FATAL_ERROR "LV_ENABLE_TSAN cannot be combined with ASan or UBSan.")
endif()

set(_lv_sanitizers "")
if(LV_ENABLE_ASAN)
    list(APPEND _lv_sanitizers address)
endif()
if(LV_ENABLE_UBSAN)
    list(APPEND _lv_sanitizers undefined)
endif()
if(LV_ENABLE_TSAN)
    list(APPEND _lv_sanitizers thread)
endif()

if(_lv_sanitizers)
    if(MSVC)
        message(FATAL_ERROR "Sanitizer presets are supported on GCC/Clang only.")
    endif()
    list(JOIN _lv_sanitizers "," _lv_sanitizer_list)
    message(STATUS "LogViewer: sanitizers enabled: ${_lv_sanitizer_list}")
    target_compile_options(
        lv_compile_options INTERFACE -fsanitize=${_lv_sanitizer_list} -fno-omit-frame-pointer
                                     -fno-sanitize-recover=all
    )
    target_link_options(lv_compile_options INTERFACE -fsanitize=${_lv_sanitizer_list})
endif()
