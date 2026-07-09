# Player module: first-person controller, voxel collision, and block ray casts.

add_library(snt_player STATIC
    player_controller.cpp
    ray_cast.cpp
    voxel_collision.cpp
)
target_include_directories(snt_player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_player PUBLIC
    snt_engine_settings
    snt_core
    snt_ecs
    snt_input
    snt_data_world
    snt_voxel
)
target_link_libraries(snt_player PRIVATE snt_third_party)
