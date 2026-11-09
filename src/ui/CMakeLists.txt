add_library(
    lv_ui STATIC
    bookmarks_panel.cpp
    bookmarks_panel.h
    document_tab.cpp
    document_tab.h
    filter_bar.cpp
    find_bar.cpp
    find_bar.h
    filter_bar.h
    go_to_line_dialog.cpp
    highlight_dialog.cpp
    highlight_dialog.h
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
