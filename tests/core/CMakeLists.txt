add_executable(lv_core_tests decode_test.cpp encoding_test.cpp line_index_test.cpp line_splitter_test.cpp literal_matcher_test.cpp log_test.cpp regex_matcher_test.cpp sorted_u64_sequence_test.cpp util_test.cpp version_test.cpp)

target_link_libraries(lv_core_tests PRIVATE lv::core lv::compile_options Catch2::Catch2WithMain)

catch_discover_tests(
    lv_core_tests
    TEST_PREFIX "core."
    PROPERTIES LABELS core
    DISCOVERY_MODE PRE_TEST
)

# File-system integration tests (label fs): real files, sharing, rename, delete.
add_executable(lv_fs_tests file_test.cpp indexer_test.cpp line_reader_test.cpp)
target_link_libraries(lv_fs_tests PRIVATE lv::core lv::loggen lv_test_support lv::compile_options Catch2::Catch2WithMain)
catch_discover_tests(
    lv_fs_tests
    TEST_PREFIX "fs."
    PROPERTIES LABELS fs
    DISCOVERY_MODE PRE_TEST
)
