# Render backend: Vulkan 1.3 + VMA.
# P1.5: mesh rendering (depth buffer, descriptor sets, .obj loading).

add_library(snt_render_backend STATIC
    vma_impl.cpp
    vulkan_instance.cpp
    vulkan_device.cpp
    vulkan_swapchain.cpp
    vulkan_pipeline.cpp
    vulkan_buffer.cpp
    vulkan_descriptor.cpp
    vulkan_depth.cpp
    vulkan_mesh.cpp
    vulkan_frame.cpp
    command_context.cpp
    vertex_buffer_pool.cpp
)
target_include_directories(snt_render_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# VMA include path: FetchContent puts source in _deps/vulkanmemoryallocator-src/.
# PUBLIC because vulkan_buffer.h / vulkan_depth.h (consumed by engine hosts)
# include <vk_mem_alloc.h>.
target_include_directories(snt_render_backend PUBLIC
    ${CMAKE_BINARY_DIR}/_deps/vulkanmemoryallocator-src/include
)
# tinyobjloader header is in the source root, not in a subdirectory.
target_include_directories(snt_render_backend PRIVATE
    ${CMAKE_BINARY_DIR}/_deps/tinyobjloader-src
)
target_link_libraries(snt_render_backend PUBLIC snt_engine_settings snt_third_party)

# ============================================================
# Shader compilation: GLSL -> SPIR-V via glslangValidator
# ============================================================
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "D:/vulkansdk/Bin"
        "C:/VulkanSDK"
)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Compile each .vert/.frag shader into a .spv file.
set(SHADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/triangle.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/triangle.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/mesh.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/mesh.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/voxel.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/voxel.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/ui.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/ui.frag
)

set(SHADER_SPV_FILES "")
foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
    list(APPEND SHADER_SPV_FILES ${SPV_FILE})

    if(GLSLANG_VALIDATOR)
        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPV_FILE}
            DEPENDS ${SHADER}
            COMMENT "Compiling GLSL shader: ${SHADER_NAME}"
        )
    else()
        message(FATAL_ERROR "glslangValidator not found. "
            "Install Vulkan SDK or set VULKAN_SDK env var.")
    endif()
endforeach()

add_custom_target(snt_shaders ALL DEPENDS ${SHADER_SPV_FILES})

# The host application owns packaging. Export this generated directory through
# CMake so a game can copy shaders into any runtime layout without the engine
# assuming a parent repository or an output subdirectory name.
set(SNT_SHADER_OUTPUT_DIR "${SHADER_OUTPUT_DIR}" CACHE INTERNAL
    "Directory containing compiled SNT engine shaders")