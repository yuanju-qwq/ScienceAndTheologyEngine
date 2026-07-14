# ECS module: EnTT-based entity-component-system.
# Owns generic World, stable identity, neutral simulation components, and
# scheduler contracts. Presentation and game semantics are intentionally
# outside this target.
# SystemScheduler: fixed-tick main-thread ownership, immutable worker
# snapshots, resource-conflict DAGs, and command-buffer barriers.

add_library(snt_ecs STATIC
    entity_guid.cpp
    world.cpp
    world_command_queue.cpp
    system_scheduler.cpp
)
target_include_directories(snt_ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_ecs PUBLIC
    snt_engine_settings
    EnTT::EnTT
    snt_core
)
