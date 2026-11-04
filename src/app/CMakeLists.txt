qt_add_executable(lv_app main.cpp)

set_target_properties(
    lv_app
    PROPERTIES OUTPUT_NAME logviewer
               WIN32_EXECUTABLE ON
               MACOSX_BUNDLE OFF
)

target_link_libraries(lv_app PRIVATE lv::ui lv::compile_options)

if(WIN32)
    set(LV_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/logviewer.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/logviewer.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/logviewer.rc"
        @ONLY
    )
    target_sources(
        lv_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/logviewer.rc"
                       "${CMAKE_CURRENT_SOURCE_DIR}/resources/logviewer.manifest"
    )
endif()

install(TARGETS lv_app RUNTIME DESTINATION .)

if(WIN32)
    # App-local MSVC runtime DLLs, so the portable zip runs without the VC++ redistributable.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
    include(InstallRequiredSystemLibraries)
endif()
