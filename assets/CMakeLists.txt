# Asset boundary split.
#
# snt_asset_content is Vulkan-free and owns source I/O, immutable catalog
# parsing, stable mesh identity, and generic file/image helpers. It may be
# linked by SimulationRuntime and headless tools.
#
# snt_gpu_assets owns render-thread GPU residency. It depends on both content
# and Vulkan and is only linked by ClientRuntime/presentation modules.

add_library(snt_asset_content STATIC
    asset_catalog.cpp
    asset_manifest.cpp
    filesystem_asset_source.cpp
    loader.cpp
    mesh_asset_reference_registry.cpp
    texture_cache.cpp
)
target_include_directories(snt_asset_content PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_asset_content PUBLIC
    snt_engine_settings
    snt_core
)
# Manifest parsing is implementation-only; no broad third-party aggregate is
# linked here so SimulationRuntime stays outside the SDL/Vulkan closure.
target_link_libraries(snt_asset_content PRIVATE
    nlohmann_json::nlohmann_json
    stb::stb
)

add_library(snt_gpu_assets STATIC
    asset_manager.cpp
    font_atlas.cpp
    material_atlas.cpp
    shader_cache.cpp
    vulkan_gpu_asset_uploader.cpp
)
target_include_directories(snt_gpu_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_gpu_assets PUBLIC
    snt_engine_settings
    snt_core
    snt_asset_content
    snt_render_backend
)
target_link_libraries(snt_gpu_assets PRIVATE
    Freetype::Freetype
    nlohmann_json::nlohmann_json
    shaderc::shaderc_shared
)
