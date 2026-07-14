# Runtime module: game-agnostic lifecycle and presentation orchestration.
#
# Runtime owns platform, shared services, ECS infrastructure, game-content
# source/catalog bootstrap, and generic voxel/UI presentation. Game content
# enters only through IGameSession.

add_library(snt_engine STATIC
    runtime.cpp
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
    snt_ui
    snt_voxel
    snt_voxel_data
)
target_link_libraries(snt_engine PRIVATE
    snt_render_components
    snt_script
)
