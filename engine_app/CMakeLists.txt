# Engine application layer: thin executable wrapper around Engine class.
# P2.B1: main.cpp is reduced to Engine::init/run/shutdown; all subsystem
# setup lives in the snt_engine static lib.
# P2.E: copies config/engine.json next to the executable so the runtime
# relative path "config/engine.json" resolves from the bin/$<CONFIG>/ cwd.

add_executable(snt_engine_app
    main.cpp
)
target_link_libraries(snt_engine_app PRIVATE
    snt_engine_settings
    snt_engine
)
# Ensure shaders are compiled + copied before the engine runs.
add_dependencies(snt_engine_app snt_copy_shaders)

# Copy the engine config file next to the executable. Missing file is
# non-fatal at runtime (EngineConfig falls back to defaults), but having
# the JSON present lets users tweak settings without rebuilding.
#
# Source path: the config/ directory at the repository root (one level
# above CMAKE_SOURCE_DIR, which points at engine/). Keeping the config
# outside engine/ lets it be shared with the legacy Godot project and
# avoids duplicating it under engine/.
add_custom_target(snt_copy_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/../config
    ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/config
    COMMENT "Copying engine config to output directory"
)
add_dependencies(snt_engine_app snt_copy_config)

# Copy the scenes/ directory (binary scene files) next to the executable.
# Engine::init loads config.scene.path (default "scenes/default_scene.bin")
# via path_utils::resolve, which prepends the engine root. Missing file
# is fatal to Engine::init (load_scene returns an Error), so the scenes
# must be present at runtime.
add_custom_target(snt_copy_scenes ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/../scenes
    ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/scenes
    COMMENT "Copying scenes to output directory"
)
add_dependencies(snt_engine_app snt_copy_scenes)

# Output name for convenience.
set_target_properties(snt_engine_app PROPERTIES OUTPUT_NAME snt_engine)
