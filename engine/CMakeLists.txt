# Engine module: top-level orchestrator.
# P2.B1: Engine class wraps Window + Vulkan + World + InputSystem + the
# per-frame loop. main.cpp becomes a thin entry point.
#
# Layering: sits at the application layer alongside engine_app. Kept as
# a separate static lib so engine_app stays a trivial executable wrapper.

add_library(snt_engine STATIC
    engine.cpp
)
target_include_directories(snt_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_engine PUBLIC
    snt_engine_settings
    snt_platform
    snt_core
    snt_input
    snt_ecs
    snt_assets
    snt_render
    snt_render_backend
    snt_renderer
    snt_scene
    snt_ui
)
