qt_add_executable(lv_ui_tests main_window_test.cpp)
set_target_properties(lv_ui_tests PROPERTIES WIN32_EXECUTABLE OFF)

target_link_libraries(lv_ui_tests PRIVATE lv::ui Qt6::Test lv::compile_options)

add_test(NAME ui.MainWindow COMMAND lv_ui_tests)
set_tests_properties(
    ui.MainWindow PROPERTIES LABELS ui ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
