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

# Some generators place the executable under engine_app/$<CONFIG>/ rather
# than the top-level bin/$<CONFIG>/ directory. Put runtime resources beside
# the actual executable as well, so path_utils can anchor from the exe dir.
add_custom_command(TARGET snt_engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/render_backend/shaders
        $<TARGET_FILE_DIR:snt_engine_app>/snt_engine/shaders
    COMMENT "Copying shaders next to executable"
)

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
add_custom_command(TARGET snt_engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SNT_GAME_CONTENT_ROOT}/game/config
        $<TARGET_FILE_DIR:snt_engine_app>/game/config
    COMMENT "Copying game config next to executable"
)

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
add_custom_command(TARGET snt_engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${SNT_GAME_CONTENT_ROOT}/game/scenes
    $<TARGET_FILE_DIR:snt_engine_app>/game/scenes
    COMMENT "Copying game scenes next to executable"
)

# P7.1 gameplay AngelScript content. ScriptManager watches this directory at
# runtime, so source scripts are intentionally copied as editable text.
add_custom_target(snt_copy_scripts ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${SNT_GAME_CONTENT_ROOT}/game/scripts
    ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/game/scripts
    COMMENT "Copying gameplay scripts to output directory"
)
add_dependencies(snt_engine_app snt_copy_scripts)
add_custom_command(TARGET snt_engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${SNT_GAME_CONTENT_ROOT}/game/scripts
    $<TARGET_FILE_DIR:snt_engine_app>/game/scripts
    COMMENT "Copying gameplay scripts next to executable"
)

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
add_custom_command(TARGET snt_engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SNT_GAME_CONTENT_ROOT}/test_assets
        $<TARGET_FILE_DIR:snt_engine_app>/test_assets
    COMMENT "Copying test assets next to executable"
)

add_custom_command(TARGET snt_engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SNT_GAME_CONTENT_ROOT}/resource/terrain
        $<TARGET_FILE_DIR:snt_engine_app>/resource/terrain
    COMMENT "Copying terrain textures next to executable"
)

add_custom_command(TARGET snt_engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:snt_engine_app>
        $<TARGET_FILE_DIR:snt_engine_app>
    COMMAND_EXPAND_LISTS
    COMMENT "Copying runtime DLLs next to executable"
)

# Output name for convenience.
set_target_properties(snt_engine_app PROPERTIES OUTPUT_NAME snt_engine)
