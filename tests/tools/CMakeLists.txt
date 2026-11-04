add_executable(lv_tools_tests loggen_test.cpp)

target_link_libraries(lv_tools_tests PRIVATE lv::loggen lv::compile_options Catch2::Catch2WithMain)

catch_discover_tests(
    lv_tools_tests
    TEST_PREFIX "tools."
    PROPERTIES LABELS tools
    DISCOVERY_MODE PRE_TEST
)
