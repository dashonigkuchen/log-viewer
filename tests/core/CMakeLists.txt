add_executable(lv_core_tests version_test.cpp)

target_link_libraries(lv_core_tests PRIVATE lv::core lv::compile_options Catch2::Catch2WithMain)

catch_discover_tests(
    lv_core_tests
    TEST_PREFIX "core."
    PROPERTIES LABELS core
    DISCOVERY_MODE PRE_TEST
)
