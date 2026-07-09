# World module: chunk registry + world ECS components.
# P2 task 4: extracted from WorldData (radical refactor).
# Namespace: snt::data.
#
# This is a header-only module (INTERFACE library) because ChunkRegistry is
# fully inline and world_components.h is data-only. The .cpp implementations
# live in the dependent modules (save, mobile_structure, ecs).

add_library(snt_data_world INTERFACE)
target_include_directories(snt_data_world INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_data_world INTERFACE
    snt_data_defs
    snt_data_worldgen
    snt_engine_settings
)
