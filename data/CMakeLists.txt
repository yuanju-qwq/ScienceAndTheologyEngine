# Data module: pure-C++ data definitions ported from src/core/.
# P2 task 1: data definition layer (no algorithms).
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
