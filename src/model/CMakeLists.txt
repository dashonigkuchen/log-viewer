add_library(lv_model STATIC model_info.cpp)
add_library(lv::model ALIAS lv_model)

target_link_libraries(
    lv_model
    PUBLIC lv::core Qt6::Core
    PRIVATE lv::compile_options
)
