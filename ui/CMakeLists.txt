# UI: generic retained MUI, Arc2D, Unicode text, and Vulkan UI renderer.

add_library(snt_ui STATIC
    mui_renderer.cpp
    mod_ui.cpp
    mod_ui.h
    mod_ui_internal.h
    mui_gameplay_controls.cpp
    mui_gameplay_controls.h
    ui_draw_data.h
    ui_image_registry.cpp
    ui_image_registry.h
    ui_packed_scene.cpp
    ui_packed_scene.h
    ui_packed_scene_catalog.cpp
    ui_packed_scene_catalog.h
    retained_mui_input_router.cpp
    retained_mui_input_router.h
    retained_mui_view.cpp
    retained_mui_runtime.cpp
    retained_mui_arc.cpp
    retained_mui_arc.h
    retained_mui_runtime.h
    retained_mui_screen_stack.cpp
    retained_mui_screen_stack.h
    retained_mui_text.cpp
    retained_mui_text.h
    retained_mui_text_input_service.cpp
    retained_mui_text_input_service.h
    retained_mui_types.h
    retained_mui_utf8.h
    retained_mui_view.h
    retained_mui_view_model.cpp
    retained_mui_view_model.h
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
