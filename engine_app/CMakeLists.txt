# Engine application layer: thin executable wrapper around Engine class.
# P2.B1: main.cpp is reduced to Engine::init/run/shutdown; all subsystem
# setup lives in the snt_engine static lib.
# P2.E: copies game/config/engine.json next to the executable so the runtime
# relative path "game/config/engine.json" resolves from the bin/$<CONFIG>/ cwd.

add_executable(snt_engine_app
    main.cpp
)
target_link_libraries(snt_engine_app PRIVATE
    snt_engine_settings
    snt_engine
)
# Ensure shaders are compiled + copied before the engine runs.
add_dependencies(snt_engine_app snt_copy_shaders)

# Locate game content whether this CMake project is configured from the
# repository root or directly from the snt_engine/ subdirectory.
set(SNT_GAME_CONTENT_ROOT "${CMAKE_SOURCE_DIR}")
if(NOT EXISTS "${SNT_GAME_CONTENT_ROOT}/game/config"
   AND EXISTS "${CMAKE_SOURCE_DIR}/../game/config")
    get_filename_component(SNT_GAME_CONTENT_ROOT
        "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
endif()

# Copy the game config directory next to the executable. Missing file is
# non-fatal at runtime (EngineConfig falls back to defaults), but having
# the JSON present lets users tweak settings without rebuilding.
#
# Source path: game/config/ at the project root (CMAKE_SOURCE_DIR points
# at the project root since the top-level CMakeLists.txt is there).
# The game/ directory holds all game-specific content (config, scenes,
# assets), separate from engine built-in resources (shaders/, builtin/).
add_custom_target(snt_copy_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${SNT_GAME_CONTENT_ROOT}/game/config
    ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/game/config
    COMMENT "Copying game config to output directory"
)
add_dependencies(snt_engine_app snt_copy_config)

# Copy the game scenes directory (binary scene files) next to the executable.
# Engine::init loads config.scene.path (default "game/scenes/default_scene.bin")
# via path_utils::resolve. Missing file is fatal to Engine::init (load_scene
# returns an Error), so the scenes must be present at runtime.
add_custom_target(snt_copy_scenes ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${SNT_GAME_CONTENT_ROOT}/game/scenes
    ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/game/scenes
    COMMENT "Copying game scenes to output directory"
)
add_dependencies(snt_engine_app snt_copy_scenes)

# Copy the test_assets/ directory (placeholder meshes for development).
# Engine::init loads config.assets.default_mesh_path
# (default "test_assets/cube.obj") via path_utils::resolve. The test_assets/
# directory holds placeholder resources used during engine development;
# real games will use their own assets/ directory under game/.
add_custom_target(snt_copy_test_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${SNT_GAME_CONTENT_ROOT}/test_assets
    ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/test_assets
    COMMENT "Copying test assets to output directory"
)
add_dependencies(snt_engine_app snt_copy_test_assets)

# Output name for convenience.
set_target_properties(snt_engine_app PROPERTIES OUTPUT_NAME snt_engine)
