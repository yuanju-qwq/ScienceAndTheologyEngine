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

# Main engine test executable links the broader integration TUs together.
# Small headless boundary tests below stay separate when that avoids pulling
# renderer/UI dependencies into a generic subsystem's test closure.
add_executable(snt_tests
    test_main.cpp              # GoogleTest main entry (no TEST()s here)
    test_json.cpp              # project JSON facade over the Zig ABI
    test_runtime_config.cpp
    test_abi_contract.cpp        # C-compatible runtime and render snapshot ABI
    test_abi_c_smoke.c           # compile/link the public ABI from a C TU
    test_simulation_runtime_observer.cpp  # simulation observer value contract
    test_clock.cpp
    test_path_utils.cpp
    test_asset_handle.cpp
    test_asset_pipeline.cpp
    test_asset_boundary.cpp   # staged source / GPU residency contract
    test_asset_catalog.cpp
    test_filesystem_asset_source.cpp
    test_script_engine.cpp
    test_script_loader.cpp
    test_entity_guid.cpp       # Phase 1: EntityGuid + World mapping
    test_render_components.cpp # generic mesh-LOD selection contract
    test_asset_manifest.cpp    # Asset manifest + mesh-reference identity
    test_binary_io.cpp         # Phase 3: BinaryWriter/Reader + Serializer<T>
    test_scene_serialize.cpp   # Phase 4: Scene binary save/load round-trip
    test_job_system.cpp        # P2: work-stealing Job System
    test_hash.cpp              # FNV-1a hash utilities
    test_uuid.cpp              # general-purpose 128-bit Uuid + generator
    test_voxel_data.cpp        # generic voxel chunks and raw region framing
    test_ecs.cpp               # P2: standard components + SystemScheduler
    test_player_physics.cpp    # P4: voxel collision + DDA ray cast
    test_mui_retained.cpp      # P6: retained MUI primitives
)
target_compile_definitions(snt_tests PRIVATE
    SNT_ENGINE_TEST_ROOT="${SNT_ENGINE_SOURCE_DIR}"
)
target_link_libraries(snt_tests PRIVATE
    snt_abi
    snt_core
    snt_ecs
    snt_asset_content
    snt_gpu_assets
    snt_render_components
    snt_ui
    snt_script
    snt_engine_settings
    snt_third_party
    snt_scene                  # Phase 4: scene module under test
    snt_voxel_data             # generic terrain/chunk ownership boundary
    snt_voxel_storage          # current raw region framing
    snt_player                 # P4: player collision/raycast/controller
    snt_simulation_runtime      # simulation observer public contract
    GTest::gtest
)

# Register tests with CTest. DISCOVERY_MODE PRE_TEST runs the discovery
# step at test time (not build time) so cross-compiling / multi-config
# generators like Visual Studio handle the right config's binary.
include(GoogleTest)
gtest_discover_tests(snt_tests DISCOVERY_MODE PRE_TEST)

add_custom_command(TARGET snt_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:snt_tests>
        $<TARGET_FILE_DIR:snt_tests>
    COMMAND_EXPAND_LISTS
    COMMENT "Copying runtime DLLs next to tests"
)

# This executable links only the SDL/Vulkan-free simulation target. It proves
# the dedicated-server lifecycle can initialize and tick without client code.
add_executable(snt_simulation_runtime_tests
    test_simulation_runtime.cpp
)
target_compile_definitions(snt_simulation_runtime_tests PRIVATE
    SNT_ENGINE_TEST_ROOT="${SNT_ENGINE_SOURCE_DIR}"
)
target_link_libraries(snt_simulation_runtime_tests PRIVATE
    snt_engine_settings
    snt_simulation_runtime
    GTest::gtest_main
)
gtest_discover_tests(snt_simulation_runtime_tests DISCOVERY_MODE PRE_TEST)

# RuntimeKeyIndex is a generic core boundary used by game worker snapshots.
# Keep its tests free of the renderer/UI closure so deterministic key mapping
# stays cheap to validate in both local development and headless CI.
add_executable(snt_runtime_key_index_tests
    test_runtime_key_index.cpp
)
target_link_libraries(snt_runtime_key_index_tests PRIVATE
    snt_core
    snt_engine_settings
    GTest::gtest_main
)
gtest_discover_tests(snt_runtime_key_index_tests DISCOVERY_MODE PRE_TEST)

# Replication tests intentionally link only the core/network closure. They
# exercise localhost TCP+UDP and the Steam backend adapter without SDL,
# Vulkan, platform windows, or game code.
add_executable(snt_network_tests
    test_network.cpp
)
target_link_libraries(snt_network_tests PRIVATE
    snt_engine_settings
    snt_network
    GTest::gtest_main
)
gtest_discover_tests(snt_network_tests DISCOVERY_MODE PRE_TEST)
