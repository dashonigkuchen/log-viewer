add_library(lv_ui STATIC main_window.cpp main_window.h)
add_library(lv::ui ALIAS lv_ui)

target_link_libraries(
    lv_ui
    PUBLIC lv::model Qt6::Widgets
    PRIVATE lv::compile_options
)
