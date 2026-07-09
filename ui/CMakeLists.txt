# UI: MUI (immediate-mode GUI) + Debug Panel.
# P1: stub implementations (no rendering). P1.4: Vulkan-backed rendering.

add_library(snt_ui STATIC
    mui.cpp
    mui_renderer.cpp
    debug_panel.cpp
)
target_include_directories(snt_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_ui PUBLIC
    snt_engine_settings
    snt_core
    snt_assets
    snt_render_backend
)
