# UI: generic retained MUI, Arc2D, Unicode text, and Vulkan UI renderer.

add_library(snt_ui STATIC
    mui_renderer.cpp
    ui_draw_data.h
    ui_image_registry.cpp
    ui_image_registry.h
    ui_packed_scene.cpp
    ui_packed_scene.h
    ui_packed_scene_catalog.cpp
    ui_packed_scene_catalog.h
    retained_mui.cpp
)
target_include_directories(snt_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_ui PUBLIC
    snt_engine_settings
    snt_core
    snt_asset_content
    snt_render_backend
    snt_icu
    snt_harfbuzz
)
target_link_libraries(snt_ui PRIVATE
    nlohmann_json::nlohmann_json
)
