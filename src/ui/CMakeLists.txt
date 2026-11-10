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
    settings_dialog.cpp
    settings_dialog.h
    shortcuts.cpp
    shortcuts.h
    single_instance.cpp
    single_instance.h
    main_window.cpp
    main_window.h
    overview_bar.cpp
    overview_bar.h
    text_selection.cpp
    text_selection.h
)
add_library(lv::ui ALIAS lv_ui)

target_link_libraries(
    lv_ui
    PUBLIC lv::model Qt6::Widgets Qt6::Network
    PRIVATE lv::compile_options
)
