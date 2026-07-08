# Scene module: binary scene serialization + loading.
#
# Header-only: save_scene/load_scene are function templates (parameterized
# on the mesh asset type so tests can use a StubMesh instead of VulkanMesh).
# All implementation lives in scene.h; there is no scene.cpp.
#
# Layering: depends on assets/ (AssetCache), core/ (BinaryReader/Writer,
# Serializer, Expected), ecs/ (World, components). The AssetCache template
# parameter means the header includes the asset type, but scene code
# does NOT call any Vulkan functions.
#
# This is an INTERFACE library (header-only) — no sources to compile.

add_library(snt_scene INTERFACE)
target_include_directories(snt_scene INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_scene INTERFACE
    snt_engine_settings
    snt_core
    snt_ecs
    snt_assets
)
