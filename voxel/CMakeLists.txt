# Voxel module: meshing algorithms + chunk renderer + ECS render system.
#
# Layering (top -> bottom, never reverse):
#   engine -> voxel -> {voxel_mesh (pure algo), render_backend, ecs, data}
#   voxel_mesh is standalone (no Vulkan) so it can build headless for
#   server-side collision + tests.
#
# P3 task 1: snt_voxel_mesh (greedy_mesh + collision_faces).
# P3 task 4/5: snt_voxel (chunk_renderer + chunk_render_system).

# --- Pure meshing algorithms (no Vulkan, no ECS) ---
add_library(snt_voxel_mesh STATIC
    greedy_mesh.cpp
    collision_faces.cpp
)
target_include_directories(snt_voxel_mesh PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_voxel_mesh PUBLIC snt_engine_settings)

# --- Chunk renderer + ECS render system (Vulkan + ECS + data) ---
add_library(snt_voxel STATIC
    chunk_renderer.cpp
    chunk_render_system.cpp
)
target_include_directories(snt_voxel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_voxel PUBLIC
    snt_engine_settings
    snt_voxel_mesh
    snt_render_backend
    snt_ecs
    snt_data_world
    snt_third_party            # glm
)
