# Gameplay module: deterministic content execution systems.

add_library(snt_gameplay STATIC
    machine_tick_system.cpp
)
target_include_directories(snt_gameplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(snt_gameplay PUBLIC
    snt_engine_settings
    snt_ecs
)
target_link_libraries(snt_gameplay PRIVATE snt_core snt_script)
