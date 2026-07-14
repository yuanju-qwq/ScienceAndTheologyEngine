# Self-built engine root.
# See docs/自研引擎架构设计.md for architecture baseline.

# ============================================================
# Project setup
# ============================================================
# cmake_minimum_required MUST come before project() so CMake sets up
# policies correctly. CMP0079=NEW allows target_link_libraries() to
# modify targets defined in subdirectories (e.g. adding Vulkan::Headers
# to the volk target fetched via FetchContent). Without this, the
# `target_link_libraries(volk PUBLIC Vulkan::Headers)` call in
# fetch_third_party.cmake fails with "Attempt to add link library ...
# to target ... which is not built in this directory."
cmake_minimum_required(VERSION 3.20)
project(snt_engine CXX)
# Public CMake integration contract for an embedding game. This is the only
# source-tree location the game layer may use while developing via submodule.
set(SNT_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "SNT engine source root")
cmake_policy(SET CMP0079 NEW)

if(MSVC)
    # Treat all C/C++ sources as UTF-8. This removes MSVC C4819 warnings on
    # Chinese comments/UTF-8 third-party headers while avoiding ASM/MASM files
    # such as AngelScript's x64 callfunc source.
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>)
endif()

# ============================================================
# Engine-wide interface target for shared settings
# ============================================================
# Defined BEFORE add_subdirectory so all modules can link to it.
# Engine C++ targets link to snt_engine_settings (directly or transitively
# via snt_core) and inherit these MSVC conformance flags. Third-party
# targets fetched via FetchContent do NOT link to this interface, so they
# keep their own CMake-configured flags — critical because some of them
# (e.g. AngelScript's MASM source) cannot tolerate C++ flags like
# /Zc:__cplusplus being pushed into their ASM compile step.
add_library(snt_engine_settings INTERFACE)
target_compile_features(snt_engine_settings INTERFACE cxx_std_20)
if(MSVC)
    target_compile_options(snt_engine_settings INTERFACE
        /permissive-
        /Zc:__cplusplus
        /EHsc
        /FS
    )
endif()

# Module targets own their source-root include visibility. Keeping this out of
# snt_engine_settings prevents compiler configuration from silently granting
# every target access to every engine header. The audit registers each module
# after target creation and validates direct #include -> link relationships.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/module_dependency_audit.cmake)

# ============================================================
# Third-party dependencies
# ============================================================
# Heavy libs (SDL3) come from extracted source under third_party/.
# Header-only libs (VMA, EnTT, stb, nlohmann_json, vulkan-headers) are
# fetched via FetchContent from pre-downloaded archives.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/fetch_third_party.cmake)

# ============================================================
# Engine modules
# ============================================================
# Each module is its own static library for clear layering.
# Layering rule (top -> bottom, never reverse):
#   host application -> {client_runtime, simulation_runtime}
#   client_runtime -> {ui, render, voxel, platform, input, gpu_assets}
#   simulation_runtime -> {ecs, script, voxel_data/storage, asset_content}
#                         -> renderer -> render_backend -> platform
#                         -> ecs -> input -> core
#                         -> voxel data/storage -> core
add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(network)      # transport-neutral replication + direct sockets
add_subdirectory(input)
add_subdirectory(render_backend)
add_subdirectory(assets)        # source/catalog/reference + GPU uploader (depends on render_backend)
add_subdirectory(renderer)
add_subdirectory(ecs)
add_subdirectory(render)
add_subdirectory(script)        # NEW: AngelScript integration (depends on core)
add_subdirectory(scene)         # NEW: binary scene serialization (depends on ecs + assets)
add_subdirectory(ui)
add_subdirectory(voxel)         # Voxel meshing and chunk renderer
add_subdirectory(player)        # First-person control, collision, and raycast
add_subdirectory(engine)

# The module/header layout remains stable for source compatibility inside the
# engine. Registering the ownership map here lets CMake verify the dependency
# graph without pretending that the facade target supplies downstream headers.
snt_register_engine_module(
    TARGET snt_platform
    INCLUDE_PREFIXES platform
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/platform"
)
snt_register_engine_module(
    TARGET snt_core
    INCLUDE_PREFIXES core
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/core"
)
snt_register_engine_module(
    TARGET snt_network
    INCLUDE_PREFIXES network
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/network"
)
snt_register_engine_module(
    TARGET snt_input
    INCLUDE_PREFIXES input
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/input"
)
snt_register_engine_module(
    TARGET snt_render_backend
    INCLUDE_PREFIXES render_backend
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/render_backend"
)
snt_register_engine_module(
    TARGET snt_asset_content
    INCLUDE_PREFIXES
        assets/asset_catalog.h
        assets/asset_handle.h
        assets/asset_manifest.h
        assets/asset_source.h
        assets/filesystem_asset_source.h
        assets/mesh_asset_reference_registry.h
    SOURCE_FILES
        "${SNT_ENGINE_SOURCE_DIR}/assets/asset_catalog.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/asset_catalog.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/asset_handle.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/asset_manifest.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/asset_manifest.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/asset_source.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/filesystem_asset_source.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/filesystem_asset_source.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/loader.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/loader.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/mesh_asset_reference_registry.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/mesh_asset_reference_registry.h"
)
snt_register_engine_module(
    TARGET snt_gpu_assets
    INCLUDE_PREFIXES
        assets/asset_manager.h
        assets/font_atlas.h
        assets/gpu_asset_uploader.h
        assets/material_atlas.h
        assets/shader_cache.h
        assets/texture_cache.h
        assets/vulkan_gpu_asset_uploader.h
    SOURCE_FILES
        "${SNT_ENGINE_SOURCE_DIR}/assets/asset_manager.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/asset_manager.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/font_atlas.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/font_atlas.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/gpu_asset_uploader.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/material_atlas.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/material_atlas.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/shader_cache.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/shader_cache.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/texture_cache.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/texture_cache.h"
        "${SNT_ENGINE_SOURCE_DIR}/assets/vulkan_gpu_asset_uploader.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/assets/vulkan_gpu_asset_uploader.h"
)
snt_register_engine_module(
    TARGET snt_renderer
    INCLUDE_PREFIXES renderer
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/renderer"
)
snt_register_engine_module(
    TARGET snt_ecs
    INCLUDE_PREFIXES ecs
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/ecs"
)
snt_register_engine_module(
    INTERFACE
    TARGET snt_render_components
    INCLUDE_PREFIXES render/render_components.h
    SOURCE_FILES
        "${SNT_ENGINE_SOURCE_DIR}/render/render_components.h"
)
snt_register_engine_module(
    TARGET snt_render
    INCLUDE_PREFIXES render
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/render"
)
snt_register_engine_module(
    TARGET snt_script
    INCLUDE_PREFIXES script
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/script"
)
snt_register_engine_module(
    INTERFACE
    TARGET snt_scene
    INCLUDE_PREFIXES scene
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/scene"
)
snt_register_engine_module(
    TARGET snt_ui
    INCLUDE_PREFIXES ui
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/ui"
)
snt_register_engine_module(
    INTERFACE
    TARGET snt_voxel_data
    INCLUDE_PREFIXES voxel/data
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/voxel/data"
)
snt_register_engine_module(
    TARGET snt_voxel_storage
    INCLUDE_PREFIXES voxel/storage
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/voxel/storage"
)
snt_register_engine_module(
    TARGET snt_voxel_mesh
    INCLUDE_PREFIXES voxel/greedy_mesh.h voxel/collision_faces.h voxel/voxel_vertex.h
    SOURCE_FILES
        "${SNT_ENGINE_SOURCE_DIR}/voxel/greedy_mesh.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/voxel/greedy_mesh.h"
        "${SNT_ENGINE_SOURCE_DIR}/voxel/collision_faces.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/voxel/collision_faces.h"
        "${SNT_ENGINE_SOURCE_DIR}/voxel/voxel_vertex.h"
)
snt_register_engine_module(
    TARGET snt_voxel
    INCLUDE_PREFIXES voxel/chunk_renderer.h voxel/chunk_render_system.h
    SOURCE_FILES
        "${SNT_ENGINE_SOURCE_DIR}/voxel/chunk_renderer.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/voxel/chunk_renderer.h"
        "${SNT_ENGINE_SOURCE_DIR}/voxel/chunk_render_system.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/voxel/chunk_render_system.h"
)
snt_register_engine_module(
    TARGET snt_player
    INCLUDE_PREFIXES player
    SOURCE_DIRS "${SNT_ENGINE_SOURCE_DIR}/player"
)
snt_register_engine_module(
    TARGET snt_simulation_runtime
    INCLUDE_PREFIXES
        engine/simulation_runtime.h
        engine/simulation_services.h
        engine/simulation_session.h
        engine/simulation_runtime_observer.h
    SOURCE_FILES
        "${SNT_ENGINE_SOURCE_DIR}/engine/simulation_runtime.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/engine/simulation_runtime.h"
        "${SNT_ENGINE_SOURCE_DIR}/engine/simulation_services.h"
        "${SNT_ENGINE_SOURCE_DIR}/engine/simulation_session.h"
        "${SNT_ENGINE_SOURCE_DIR}/engine/simulation_runtime_observer.h"
)
snt_register_engine_module(
    TARGET snt_client_runtime
    INCLUDE_PREFIXES
        engine/client_runtime.h
        engine/client_services.h
        engine/client_session.h
    SOURCE_FILES
        "${SNT_ENGINE_SOURCE_DIR}/engine/client_runtime.cpp"
        "${SNT_ENGINE_SOURCE_DIR}/engine/client_runtime.h"
        "${SNT_ENGINE_SOURCE_DIR}/engine/client_services.h"
        "${SNT_ENGINE_SOURCE_DIR}/engine/client_session.h"
)
snt_verify_engine_module_dependencies()

add_subdirectory(tools)         # NEW: standalone dev tools (gen_default_scene, ...)

# Tests are optional: gated by SNT_BUILD_TESTS (default ON). CI can
# disable with -DSNT_BUILD_TESTS=OFF to speed up Release builds.
if(SNT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
