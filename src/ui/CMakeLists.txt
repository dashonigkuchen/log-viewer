add_library(
    lv_ui STATIC
    go_to_line_dialog.cpp
    go_to_line_dialog.h
    log_view.cpp
    log_view.h
    main_window.cpp
    main_window.h
    text_selection.cpp
    text_selection.h
)
add_library(lv::ui ALIAS lv_ui)

target_link_libraries(
    lv_ui
    PUBLIC lv::model Qt6::Widgets
    PRIVATE lv::compile_options
)
