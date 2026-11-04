# Deterministic test log generator (roadmap M0-06). Qt-free on purpose.
add_library(lv_loggen STATIC loggen.cpp loggen.h)
add_library(lv::loggen ALIAS lv_loggen)
target_include_directories(lv_loggen PUBLIC "${PROJECT_SOURCE_DIR}/tools")
target_link_libraries(lv_loggen PRIVATE lv::compile_options)

add_executable(loggen main.cpp)
target_link_libraries(loggen PRIVATE lv::loggen lv::compile_options)

if(LV_BUILD_TESTS)
    add_test(NAME tools.loggen.help COMMAND loggen --help)
    set_tests_properties(tools.loggen.help PROPERTIES PASS_REGULAR_EXPRESSION "--profile" LABELS tools)
    add_test(NAME tools.loggen.small
             COMMAND loggen --profile small --size 1M --seed 7 --out
                     "${CMAKE_CURRENT_BINARY_DIR}/cli-smoke.log"
    )
    set_tests_properties(tools.loggen.small PROPERTIES PASS_REGULAR_EXPRESSION "Wrote [0-9]+ bytes" LABELS tools)
    add_test(NAME tools.loggen.bad-profile COMMAND loggen --profile nope --out x.log)
    set_tests_properties(tools.loggen.bad-profile PROPERTIES WILL_FAIL TRUE LABELS tools)
endif()
