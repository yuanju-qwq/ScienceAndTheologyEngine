# Assets module: central asset cache + loaders.
#
# Layering: sits between render_backend (VulkanMesh / VulkanDevice) and
# the rest of the engine (render/RenderSystem, ecs/components, scripts).
# Owns the canonical AssetHandle<T> type + Runtime-injected AssetManager service.
# FilesystemAssetSource implements the Vulkan-free content-reading boundary.
# IGpuAssetUploader remains declaration-only; AssetManager remains the current
# Vulkan mesh-cache path until GPU residency migration is explicitly scheduled.
#
# Layering rule (top -> bottom, never reverse):
#   host application -> engine -> assets -> render_backend -> core
#   render/ecs/ui also depend on assets via includes.

add_library(snt_assets STATIC
    asset_manager.cpp
    asset_catalog.cpp
    asset_manifest.cpp
    font_atlas.cpp
    filesystem_asset_source.cpp
    loader.cpp
    material_atlas.cpp
    mesh_asset_loader.cpp
    shader_cache.cpp
    texture_cache.cpp
)
target_include_directories(snt_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_assets PUBLIC
    snt_engine_settings
    snt_core
    snt_render_backend
)
# asset_manifest.cpp uses nlohmann_json for JSON parsing. Linked PRIVATE
# so the dependency doesn't leak to consumers of snt_assets (the header
# itself is dependency-free).
target_link_libraries(snt_assets PRIVATE snt_third_party stb::stb)
