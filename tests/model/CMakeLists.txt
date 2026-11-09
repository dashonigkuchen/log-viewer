add_executable(lv_model_tests main.cpp app_paths_test.cpp document_rotation_test.cpp document_tail_test.cpp document_test.cpp file_log_sink_test.cpp file_watcher_test.cpp filter_history_test.cpp filtered_document_test.cpp filtered_tail_test.cpp line_finder_test.cpp line_source_test.cpp mark_set_test.cpp match_highlighter_test.cpp recent_files_test.cpp settings_test.cpp)

target_link_libraries(
    lv_model_tests PRIVATE lv::model lv::loggen lv_test_support Qt6::Test lv::compile_options Catch2::Catch2
)

catch_discover_tests(
    lv_model_tests
    TEST_PREFIX "model."
    PROPERTIES LABELS model
    DISCOVERY_MODE PRE_TEST
)
