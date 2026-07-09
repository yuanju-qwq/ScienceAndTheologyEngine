# ECS module: EnTT-based entity-component-system.
# P1.5: World, components, systems (camera, render).
# P2.A1: CameraSystem reads input via InputSystem (no longer polls SDL).
# P2 task 5: standard gameplay components (Position/Velocity/Health/...).
# P2 task 6: SystemScheduler for main-thread / worker-pool dispatch.
# Phase 1: EntityGuid + World maintains Guid<->entity reverse map.

add_library(snt_ecs STATIC
    camera_system.cpp
    entity_guid.cpp
    world.cpp
    system_scheduler.cpp
)
target_include_directories(snt_ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_ecs PUBLIC snt_engine_settings snt_third_party snt_input)
