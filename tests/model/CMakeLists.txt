add_executable(lv_model_tests app_paths_test.cpp file_log_sink_test.cpp)

target_link_libraries(lv_model_tests PRIVATE lv::model lv::compile_options Catch2::Catch2WithMain)

catch_discover_tests(
    lv_model_tests
    TEST_PREFIX "model."
    PROPERTIES LABELS model
    DISCOVERY_MODE PRE_TEST
)
