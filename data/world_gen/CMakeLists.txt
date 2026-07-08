# World generation module: noise + config + terrain generator.
# P2 task 2: algorithm layer ported from src/core/world_gen/.
# Namespace: snt::data (was science_and_theology).
# Algorithm preserved as-is; only namespace + include paths adjusted.

add_library(snt_data_worldgen STATIC
    noise_generator.cpp
    world_gen_config.cpp
    terrain_generator.cpp
)
target_include_directories(snt_data_worldgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_data_worldgen PUBLIC snt_data_defs snt_engine_settings)
