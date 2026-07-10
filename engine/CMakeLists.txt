# Engine module: top-level orchestrator.
# P2.B1: Engine class wraps Window + Vulkan + World + InputSystem + the
# per-frame loop. main.cpp becomes a thin entry point.
#
# Layering: sits below a game-owned host application. Kept as
# a separate static lib so hosts stay thin executable wrappers.

add_library(snt_engine STATIC
    demo_world_bootstrap.cpp
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
    snt_voxel              # P3: chunk renderer + ChunkRenderSystem
    snt_player             # P4: first-person controller + voxel collision/raycast
    snt_data_world         # P3: ChunkRegistry
    snt_data_worldgen      # P3: TerrainGenerator (test chunk at origin)
    snt_script             # P7.1: AngelScript gameplay content + reload boundary
)
