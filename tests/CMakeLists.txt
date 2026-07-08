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
    test_script_engine.cpp
    test_script_loader.cpp
    test_entity_guid.cpp       # Phase 1: EntityGuid + World mapping
    test_asset_manifest.cpp    # Phase 2: asset manifest + pre-allocated handles
    test_binary_io.cpp         # Phase 3: BinaryWriter/Reader + Serializer<T>
    test_scene_serialize.cpp   # Phase 4: Scene binary save/load round-trip
)
target_link_libraries(snt_tests PRIVATE
    snt_core
    snt_ecs
    snt_assets
    snt_script
    snt_engine_settings
    snt_third_party
    snt_scene                  # Phase 4: scene module under test
    GTest::gtest
)

# Register tests with CTest. DISCOVERY_MODE PRE_TEST runs the discovery
# step at test time (not build time) so cross-compiling / multi-config
# generators like Visual Studio handle the right config's binary.
include(GoogleTest)
gtest_discover_tests(snt_tests DISCOVERY_MODE PRE_TEST)

# Copy config + assets + shaders next to the test executable so tests
# that read engine.json / cube.obj / mesh.vert.spv can find them via
# path_utils::resolve(). Mirrors the snt_engine_app copy step.
add_dependencies(snt_tests snt_copy_shaders snt_copy_config)
