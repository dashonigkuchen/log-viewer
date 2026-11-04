find_package(Catch2 3 CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)
include(Catch)

add_subdirectory(core)
add_subdirectory(model)
add_subdirectory(ui)
