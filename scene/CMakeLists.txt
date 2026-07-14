# Scene module: binary scene serialization + loading.
#
# Header-only: save_scene/load_scene depend on the Vulkan-free
# IMeshAssetReferenceResolver contract. All implementation lives in scene.h;
# there is no scene.cpp.
#
# Layering: depends on assets/ (mesh references), core/ (BinaryReader/Writer,
# Serializer, Expected), ecs/ (World), and render/ (presentation components).
# Scene code does NOT call Vulkan functions.
#
# This is an INTERFACE library (header-only) — no sources to compile.

add_library(snt_scene INTERFACE)
target_include_directories(snt_scene INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_scene INTERFACE
    snt_engine_settings
    snt_core
    snt_ecs
    snt_assets
    snt_render_components
)
