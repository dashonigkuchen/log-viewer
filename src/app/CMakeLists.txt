qt_add_executable(lv_app main.cpp)

set_target_properties(
    lv_app
    PROPERTIES OUTPUT_NAME logviewer
               WIN32_EXECUTABLE ON
               MACOSX_BUNDLE OFF
)

target_link_libraries(lv_app PRIVATE lv::ui lv::compile_options)

install(TARGETS lv_app RUNTIME DESTINATION .)
