# Runtime split.
#
# snt_simulation_runtime owns deterministic services and has no SDL/Vulkan
# dependency. snt_client_runtime composes it with local presentation services.
# A game supplies ISimulationSession to a server or IClientSession to a client.

add_library(snt_simulation_runtime STATIC
    simulation_runtime.cpp
    zig_simulation_runtime_host.cpp
)
target_include_directories(snt_simulation_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_simulation_runtime PUBLIC
    snt_engine_settings
    snt_abi
    snt_core
    snt_ecs
    snt_asset_content
    snt_script
    snt_voxel_data
)

add_library(snt_client_runtime STATIC
    client_runtime.cpp
)
target_include_directories(snt_client_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_client_runtime PUBLIC
    snt_engine_settings
    snt_simulation_runtime
    snt_core
    snt_ecs
    snt_platform
    snt_input
    snt_gpu_assets
    snt_render
    snt_render_backend
    snt_renderer
    snt_ui
    snt_voxel
    snt_voxel_data
)
target_link_libraries(snt_client_runtime PRIVATE
    snt_render_components
)
