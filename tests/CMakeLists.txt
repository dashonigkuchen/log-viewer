find_package(Catch2 3 CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)
include(Catch)

# Shared test helpers (temporary directories, simulated log writers).
add_library(lv_test_support STATIC support/log_writer.cpp)
target_include_directories(lv_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(lv_test_support PRIVATE lv::compile_options)

add_subdirectory(core)
add_subdirectory(model)
add_subdirectory(ui)
add_subdirectory(tools)
