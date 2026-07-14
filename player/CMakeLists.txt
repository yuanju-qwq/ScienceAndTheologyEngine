# Player module: first-person controller, voxel collision, and block ray casts.

add_library(snt_player STATIC
    collision_snapshot.cpp
    player_controller.cpp
    player_physics_system.cpp
    ray_cast.cpp
    voxel_collision.cpp
)
target_include_directories(snt_player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_player PUBLIC
    snt_engine_settings
    snt_core
    snt_ecs
    snt_input
    snt_voxel_data
    snt_voxel
)
target_link_libraries(snt_player PRIVATE
    snt_render_components
    snt_third_party
)
