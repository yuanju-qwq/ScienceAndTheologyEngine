# Unit tests for the SNT engine.
#
# Gated by SNT_BUILD_TESTS (default ON). Tests link against the engine
# static libs they exercise + GoogleTest. Each test executable is
# registered with CTest via gtest_discover_tests so `ctest` picks them up.

# Guard: only configure when GoogleTest was fetched (SNT_BUILD_TESTS=ON
# triggered FetchContent in fetch_third_party.cmake).
if(NOT TARGET GTest::gtest)
    return()
endif()

# Single test executable that links all test TUs together. Splitting per-
# module executables adds CMake boilerplate without value at this scale.
add_executable(snt_tests
    test_main.cpp              # GoogleTest main entry (no TEST()s here)
    test_engine_config.cpp
    test_clock.cpp
    test_path_utils.cpp
    test_asset_cache.cpp
    test_asset_pipeline.cpp
    test_script_engine.cpp
    test_script_loader.cpp
    test_registry_hub.cpp       # P7: script-owned content + transactional reload
    test_entity_guid.cpp       # Phase 1: EntityGuid + World mapping
    test_asset_manifest.cpp    # Phase 2: asset manifest + pre-allocated handles
    test_binary_io.cpp         # Phase 3: BinaryWriter/Reader + Serializer<T>
    test_scene_serialize.cpp   # Phase 4: Scene binary save/load round-trip
    test_job_system.cpp        # P2: work-stealing Job System
    test_hash.cpp              # FNV-1a hash utilities
    test_uuid.cpp              # general-purpose 128-bit Uuid + generator
    test_data.cpp              # P2: data layer (chunk/registry/save/noise/terrain)
    test_ecs.cpp               # P2: standard components + SystemScheduler
    test_player_physics.cpp    # P4: voxel collision + DDA ray cast
    test_mui_retained.cpp      # P6: retained MUI + gameplay UI ViewModels
)
target_link_libraries(snt_tests PRIVATE
    snt_core
    snt_ecs
    snt_assets
    snt_ui
    snt_script
    snt_engine_settings
    snt_third_party
    snt_scene                  # Phase 4: scene module under test
    snt_data_save              # P2: data save layer (ChunkSerializer/RegionFile)
    snt_data_worldgen          # P2: terrain generator + noise
    snt_data_world             # P2: ChunkRegistry
    snt_player                 # P4: player collision/raycast/controller
    GTest::gtest
)

# Register tests with CTest. DISCOVERY_MODE PRE_TEST runs the discovery
# step at test time (not build time) so cross-compiling / multi-config
# generators like Visual Studio handle the right config's binary.
include(GoogleTest)
gtest_discover_tests(snt_tests DISCOVERY_MODE PRE_TEST)

# Copy config + scenes + test_assets + shaders next to the test executable
# so tests that read engine.json / default_scene.bin / cube.obj /
# mesh.vert.spv can find them via path_utils::resolve(). Mirrors the
# snt_engine_app copy steps.
add_dependencies(snt_tests snt_copy_shaders snt_copy_config snt_copy_scenes snt_copy_test_assets)

add_custom_command(TARGET snt_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:snt_tests>
        $<TARGET_FILE_DIR:snt_tests>
    COMMAND_EXPAND_LISTS
    COMMENT "Copying runtime DLLs next to tests"
)
# P6 remains testable even while unrelated engine modules are in flight.
# Keep the retained-MUI contract isolated from voxel/player integration.
add_executable(snt_p6_tests
    test_main.cpp
    test_mui_retained.cpp
)
target_link_libraries(snt_p6_tests PRIVATE
    snt_core
    snt_assets
    snt_ui
    snt_engine_settings
    snt_third_party
    GTest::gtest
)
gtest_discover_tests(snt_p6_tests DISCOVERY_MODE PRE_TEST)