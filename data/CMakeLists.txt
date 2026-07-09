# Data module: pure-C++ data definitions ported from src/core/.
# P2 task 1: data definition layer (no algorithms).
# P2 task 2: world generation algorithm layer (noise + config + terrain).
# Namespace: snt::data (was science_and_theology / science_and_theology::gt).
#
# Layering: depends only on snt_engine_settings (engine-wide interface).
#           No dependency on ecs/render/render_backend — data layer is
#           standalone and can be compiled into a headless server.

add_library(snt_data_defs STATIC
    defs/creature_species.cpp
    defs/block_entity_registry.cpp
    defs/machine_collision_overlay.cpp
)
target_include_directories(snt_data_defs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_data_defs PUBLIC snt_engine_settings)

# P2 task 2: world generation algorithms (noise + config + terrain generator).
# Algorithm preserved as-is from src/core/world_gen/; only namespace + include
# paths adjusted. Depends on snt_data_defs for terrain_data/chunk_data/etc.
add_subdirectory(world_gen)

# P2 task 3: save layer (region file + chunk serializer + region compactor + save manager).
# Format preserved as-is from src/core/save/. save_manager now takes
# ChunkRegistry instead of the legacy WorldData god-object.
add_subdirectory(save)

# P2 task 4: mobile structures (ships, contraptions) ported from
# src/core/mobile_structure/. mobile_structure sub-namespace flattened into
# snt::data.
add_subdirectory(mobile_structure)

# P2 task 4: world module (ChunkRegistry + world ECS components) extracted
# from the radical WorldData refactor. Header-only INTERFACE library.
add_subdirectory(world)
