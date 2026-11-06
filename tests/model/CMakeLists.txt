add_executable(lv_model_tests main.cpp app_paths_test.cpp document_test.cpp file_log_sink_test.cpp line_source_test.cpp recent_files_test.cpp)

target_link_libraries(
    lv_model_tests PRIVATE lv::model lv::loggen lv_test_support Qt6::Test lv::compile_options Catch2::Catch2
)

catch_discover_tests(
    lv_model_tests
    TEST_PREFIX "model."
    PROPERTIES LABELS model
    DISCOVERY_MODE PRE_TEST
)
