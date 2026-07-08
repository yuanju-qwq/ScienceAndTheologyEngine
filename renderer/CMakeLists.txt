# Renderer: engine-level rendering abstraction layer above render_backend.
# P2.1: RenderGraph skeleton (declarations only, no implementation yet).
#
# Layering (top -> bottom, never reverse):
#   engine_app -> renderer -> render_backend -> platform
#
# References:
#   - KhronosGroup/Vulkan-Samples (vkb-framework) for PipelineCache / ResourceCache design.
#   - Themaister/Granite for render graph pass topology + execute callback pattern.

add_library(snt_renderer STATIC
    render_graph.cpp
    render_graph_pass.cpp
    render_graph_resource.cpp
    pipeline_cache.cpp
    transient_pool.cpp
)
target_include_directories(snt_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_renderer PUBLIC snt_engine_settings snt_core snt_render_backend)
