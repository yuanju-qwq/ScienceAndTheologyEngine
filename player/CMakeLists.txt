# Player module: first-person controller, voxel collision, and block ray casts.

# Voxel AABB collision has no input, rendering, or client-runtime dependency.
# Keep it in a small headless target so dedicated gameplay code can use the
# exact same solid-cell rules as the graphical controller.
add_library(snt_voxel_collision STATIC
    voxel_collision.cpp
)
target_include_directories(snt_voxel_collision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_voxel_collision PUBLIC
    snt_engine_settings
    snt_core
    snt_voxel_data
)

add_library(snt_player STATIC
    collision_snapshot.cpp
    player_controller.cpp
    player_physics_system.cpp
    ray_cast.cpp
)
target_include_directories(snt_player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_player PUBLIC
    snt_engine_settings
    snt_core
    snt_ecs
    snt_input
    snt_voxel_collision
    snt_voxel_data
    snt_voxel
)
target_link_libraries(snt_player PRIVATE
    snt_render_components
    snt_third_party
)
