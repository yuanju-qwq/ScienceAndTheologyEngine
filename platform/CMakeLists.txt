# Platform abstraction: window/input/filesystem via SDL3.

add_library(snt_platform STATIC
    window.cpp
)
target_include_directories(snt_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_platform PUBLIC snt_engine_settings snt_core snt_third_party)
# SDL_vulkan.h pulls <vulkan/vulkan.h> which would declare vkXxx()
# prototypes. Without linking vulkan-1.lib, the link would fail.
# VK_NO_PROTOTYPES makes the header only expose types (no prototypes),
# since Vulkan function pointers are loaded dynamically via Volk in
# render_backend. Platform layer never calls vkXxx() directly.
target_compile_definitions(snt_platform PUBLIC VK_NO_PROTOTYPES)
