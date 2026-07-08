# Tools: standalone utilities for engine development.
#
# Each tool is a separate executable. They link against engine modules
# but are NOT part of the runtime engine build (snt_engine_app doesn't
# depend on them). Tools are built by default so they're available for
# CI; individual tools can be gated with options if needed.

# gen_default_scene: generates scenes/default_scene.bin from hardcoded
# demo data. Run once after building to create the scene file; the file
# is checked into the repo so end users don't need to rebuild this tool.
add_executable(gen_default_scene
    gen_default_scene.cpp
)
target_link_libraries(gen_default_scene PRIVATE
    snt_engine_settings
    snt_scene
    snt_core
    snt_ecs
    snt_assets
)
