# Assets module: central asset cache + loaders.
#
# Layering: sits between render_backend (VulkanMesh / VulkanDevice) and
# the rest of the engine (render/RenderSystem, ecs/components, scripts).
# Owns the canonical AssetHandle<T> type + AssetManager singleton.
#
# Layering rule (top -> bottom, never reverse):
#   engine_app -> engine -> assets -> render_backend -> core
#   render/ecs/ui also depend on assets via includes.

add_library(snt_assets STATIC
    asset_manager.cpp
    asset_manifest.cpp
    mesh_asset_loader.cpp
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
target_link_libraries(snt_assets PRIVATE snt_third_party)
