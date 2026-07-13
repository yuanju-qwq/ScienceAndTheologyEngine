# Header-only presentation contract. This keeps scene and game code off the
# Vulkan RenderSystem implementation while preserving one canonical component API.
add_library(snt_render_components INTERFACE)
target_link_libraries(snt_render_components INTERFACE
    snt_engine_settings
    snt_core
    snt_assets
)

# Render module: ECS-driven rendering system.
# P2.C3: RenderSystem registers a "forward" pass with RenderGraph and
# drives rendering from ECS data (View<Transform, MeshRef>).
#
# Layering: sits beside ecs — it IS an ECS system, but needs renderer +
# render_backend to actually issue draws. Mesh loading is now delegated
# to the assets module through Runtime-injected AssetManager ownership.

add_library(snt_render STATIC
    render_system.cpp
)
target_include_directories(snt_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_render PUBLIC
    snt_engine_settings
    snt_core
    snt_ecs
    snt_assets
    snt_renderer
    snt_render_backend
)
target_link_libraries(snt_render PRIVATE snt_render_components)
