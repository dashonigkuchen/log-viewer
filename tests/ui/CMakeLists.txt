# Qt's offscreen platform finds no fonts on Windows (Qt no longer ships any), so glyphs get zero
# width and text layout tests break. Point it at the system fonts.
set(LV_UI_TEST_ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(WIN32)
    list(APPEND LV_UI_TEST_ENVIRONMENT "QT_QPA_FONTDIR=C:/Windows/Fonts")
endif()

qt_add_executable(lv_ui_tests main_window_test.cpp)
set_target_properties(lv_ui_tests PROPERTIES WIN32_EXECUTABLE OFF)

target_link_libraries(lv_ui_tests PRIVATE lv::ui Qt6::Test lv::compile_options)

add_test(NAME ui.MainWindow COMMAND lv_ui_tests -o "-,txt")
set_tests_properties(
    ui.MainWindow PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}"
)

qt_add_executable(lv_ui_log_view_tests log_view_test.cpp fake_line_source.h)
set_target_properties(lv_ui_log_view_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_log_view_tests PRIVATE lv::ui Qt6::Test lv::compile_options)
add_test(NAME ui.LogView COMMAND lv_ui_log_view_tests -o "-,txt")
set_tests_properties(ui.LogView PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_navigation_tests log_view_navigation_test.cpp fake_line_source.h)
set_target_properties(lv_ui_navigation_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_navigation_tests PRIVATE lv::ui Qt6::Test lv::compile_options)
add_test(NAME ui.LogViewNavigation COMMAND lv_ui_navigation_tests -o "-,txt")
set_tests_properties(
    ui.LogViewNavigation PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}"
)

qt_add_executable(lv_ui_selection_tests log_view_selection_test.cpp fake_line_source.h)
set_target_properties(lv_ui_selection_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(
    lv_ui_selection_tests PRIVATE lv::ui lv::loggen lv_test_support Qt6::Test lv::compile_options
)
add_test(NAME ui.LogViewSelection COMMAND lv_ui_selection_tests -o "-,txt")
set_tests_properties(
    ui.LogViewSelection PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}"
)

qt_add_executable(lv_ui_copy_tests log_view_copy_test.cpp)
set_target_properties(lv_ui_copy_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(
    lv_ui_copy_tests PRIVATE lv::ui lv::loggen lv_test_support Qt6::Test lv::compile_options
)
add_test(NAME ui.LogViewCopy COMMAND lv_ui_copy_tests -o "-,txt")
set_tests_properties(
    ui.LogViewCopy PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}"
)

qt_add_executable(lv_ui_tabs_tests main_window_tabs_test.cpp)
set_target_properties(lv_ui_tabs_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_tabs_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.MainWindowTabs COMMAND lv_ui_tabs_tests -o "-,txt")
set_tests_properties(
    ui.MainWindowTabs PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}"
)

qt_add_executable(lv_ui_recent_tests main_window_recent_test.cpp)
set_target_properties(lv_ui_recent_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_recent_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.MainWindowRecent COMMAND lv_ui_recent_tests -o "-,txt")
set_tests_properties(
    ui.MainWindowRecent PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}"
)

qt_add_executable(lv_ui_settings_tests main_window_settings_test.cpp)
set_target_properties(lv_ui_settings_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_settings_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.MainWindowSettings COMMAND lv_ui_settings_tests -o "-,txt")
set_tests_properties(
    ui.MainWindowSettings PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}"
)

qt_add_executable(lv_ui_filter_tests document_tab_filter_test.cpp)
set_target_properties(lv_ui_filter_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_filter_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.DocumentTabFilter COMMAND lv_ui_filter_tests -o "-,txt")
set_tests_properties(
    ui.DocumentTabFilter PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}"
)

qt_add_executable(lv_ui_filter_bar_tests filter_bar_test.cpp)
set_target_properties(lv_ui_filter_bar_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_filter_bar_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.FilterBar COMMAND lv_ui_filter_bar_tests -o "-,txt")
set_tests_properties(ui.FilterBar PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_highlight_tests match_highlight_test.cpp)
set_target_properties(lv_ui_highlight_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_highlight_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.MatchHighlight COMMAND lv_ui_highlight_tests -o "-,txt")
set_tests_properties(ui.MatchHighlight PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_history_tests filter_history_ui_test.cpp)
set_target_properties(lv_ui_history_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_history_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.FilterHistory COMMAND lv_ui_history_tests -o "-,txt")
set_tests_properties(ui.FilterHistory PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_follow_tests follow_mode_test.cpp)
set_target_properties(lv_ui_follow_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_follow_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.FollowMode COMMAND lv_ui_follow_tests -o "-,txt")
set_tests_properties(ui.FollowMode PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_find_bar_tests find_bar_test.cpp)
set_target_properties(lv_ui_find_bar_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_find_bar_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.FindBar COMMAND lv_ui_find_bar_tests -o "-,txt")
set_tests_properties(ui.FindBar PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

# Live stress test (M4-07): label `slow`, nightly only. Manual run: lv_ui_stress_tests -o -,txt
qt_add_executable(lv_ui_stress_tests live_stress_test.cpp)
set_target_properties(lv_ui_stress_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_stress_tests PRIVATE lv::ui lv::loggen lv_test_support Qt6::Test lv::compile_options)
if(WIN32)
    target_link_libraries(lv_ui_stress_tests PRIVATE psapi)
endif()
add_test(NAME slow.LiveStress COMMAND lv_ui_stress_tests -o "-,txt")
set_tests_properties(slow.LiveStress PROPERTIES LABELS slow TIMEOUT 300 ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_marks_tests marks_test.cpp)
set_target_properties(lv_ui_marks_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_marks_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.Marks COMMAND lv_ui_marks_tests -o "-,txt")
set_tests_properties(ui.Marks PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_bookmarks_tests bookmarks_test.cpp)
set_target_properties(lv_ui_bookmarks_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_bookmarks_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.Bookmarks COMMAND lv_ui_bookmarks_tests -o "-,txt")
set_tests_properties(ui.Bookmarks PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_highlight_rule_tests highlight_painting_test.cpp fake_line_source.h)
set_target_properties(lv_ui_highlight_rule_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_highlight_rule_tests PRIVATE lv::ui Qt6::Test lv::compile_options)
add_test(NAME ui.HighlightRules COMMAND lv_ui_highlight_rule_tests -o "-,txt")
set_tests_properties(ui.HighlightRules PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_highlight_dialog_tests highlight_dialog_test.cpp)
set_target_properties(lv_ui_highlight_dialog_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_highlight_dialog_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.HighlightDialog COMMAND lv_ui_highlight_dialog_tests -o "-,txt")
set_tests_properties(ui.HighlightDialog PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")

qt_add_executable(lv_ui_annotations_tests annotations_test.cpp)
set_target_properties(lv_ui_annotations_tests PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(lv_ui_annotations_tests PRIVATE lv::ui lv_test_support Qt6::Test lv::compile_options)
add_test(NAME ui.Annotations COMMAND lv_ui_annotations_tests -o "-,txt")
set_tests_properties(ui.Annotations PROPERTIES LABELS ui ENVIRONMENT "${LV_UI_TEST_ENVIRONMENT}")
