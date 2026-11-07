add_library(
    lv_model STATIC
    app_paths.cpp
    document.cpp
    document.h
    document_line_source.cpp
    document_line_source.h
    file_log_sink.cpp
    filtered_document.cpp
    filtered_document.h
    filtered_line_source.cpp
    filtered_line_source.h
    line_cache.cpp
    line_cache.h
    line_source.h
    model_info.cpp
    recent_files.cpp
    recent_files.h
    search_pool.cpp
    search_pool.h
    settings.cpp
    settings.h
    text_copy.cpp
    text_copy.h
)
add_library(lv::model ALIAS lv_model)

target_link_libraries(
    lv_model
    PUBLIC lv::core Qt6::Core
    PRIVATE lv::compile_options
)
