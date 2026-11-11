add_subdirectory(loggen)

if(LV_BUILD_TESTS)
    find_package(Python3 3.11 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME tools.bench_compare
                 COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bench_compare.py"
        )
        set_tests_properties(tools.bench_compare PROPERTIES LABELS tools)
        add_test(NAME tools.no_network
                 COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lint_no_network.py"
        )
        set_tests_properties(tools.no_network PROPERTIES LABELS tools)
    endif()
endif()
