// MuiRenderer — Vulkan rendering backend for retained MUI draw data.
//
// Responsibilities:
//   - Own a dynamic RGBA Unicode glyph atlas synchronized from UiDrawData.
//   - Create a UI graphics pipeline: pos2D+uv vertex layout, orthographic
//     projection UBO, alpha blending, no depth test/write (UI draws on top).
//   - Per frame: receive UiDrawData from retained MUI / Arc2D, upload to a
//     staging buffer, and record draw calls into the active command buffer.
//
// Integration: RenderSystem records UI draws at the END of its forward
// pass callback (same render pass scope, same command buffer). The UI
// pipeline switches off depth write and enables alpha blend so text
// composites over the 3D scene.
//
// Layering: sits in ui/, depends on render_backend (VulkanDevice, pipeline,
// buffer, descriptor). Retained MUI produces draw data; MuiRenderer consumes it.

#pragma once

#include "ui_draw_data.h"

#include "core/expected.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snt::render_backend {
class VulkanDevice;
class VulkanPipeline;
class VulkanBuffer;
}

namespace snt::ui {

class MuiRenderer {
public:
    MuiRenderer() = default;
    ~MuiRenderer();

    MuiRenderer(const MuiRenderer&) = delete;
    MuiRenderer& operator=(const MuiRenderer&) = delete;

    // Initialize the dynamic glyph texture, pipeline and descriptors.
    // `color_format` is the swapchain image format for the pipeline.
    snt::core::Expected<void> init(snt::render_backend::VulkanDevice& device,
                                   VkFormat color_format);

    void destroy();

    // Update the orthographic projection UBO for the current swapchain extent.
    // Call once per frame before render().
    void update_ortho(uint32_t fb_width, uint32_t fb_height);

    // Synchronize the current Unicode atlas outside a render pass. The engine
    // invokes this after retained UI frame construction and before it records
    // the UI pass, so uploads never race active Vulkan rendering.
    snt::core::Expected<void> synchronize_glyph_atlas(const UiDrawData& draw_data);

    // Record UI draw calls into `cmd`. Uploads `draw_data` vertices/indices
    // to a CPU-visible buffer and draws them with the UI pipeline.
    void render(VkCommandBuffer cmd, const UiDrawData& draw_data);

private:
    // Create and upload the fixed-capacity RGBA Unicode atlas texture.
    snt::core::Expected<void> create_glyph_atlas_texture();
    snt::core::Expected<void> upload_glyph_atlas(const UiGlyphAtlas& atlas);

    // Create the UI descriptor set layout + pool + set (UBO + sampler).
    snt::core::Expected<void> create_descriptors();

    // Create the UI graphics pipeline.
    snt::core::Expected<void> create_pipeline(VkFormat color_format);

    snt::render_backend::VulkanDevice*              device_ = nullptr;

    // Dynamic glyph atlas texture.
    VkImage        atlas_image_       = VK_NULL_HANDLE;
    VkImageView    atlas_view_        = VK_NULL_HANDLE;
    VmaAllocation  atlas_allocation_  = VK_NULL_HANDLE;
    VkSampler      atlas_sampler_     = VK_NULL_HANDLE;
    uint64_t uploaded_atlas_revision_ = 0;
    const UiGlyphAtlas* uploaded_atlas_ = nullptr;

    // Pipeline + descriptor.
    VkPipelineLayout      pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline            pipeline_        = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout_     = VK_NULL_HANDLE;
    VkDescriptorPool      desc_pool_       = VK_NULL_HANDLE;
    VkDescriptorSet       desc_set_        = VK_NULL_HANDLE;

    // UBO buffer for ortho projection (4x4 float matrix).
    VkBuffer       ubo_buffer_  = VK_NULL_HANDLE;
    VmaAllocation  ubo_alloc_   = VK_NULL_HANDLE;

    // Vertex/index buffers (CPU-visible, recreated per frame as needed).
    VkBuffer       vbo_         = VK_NULL_HANDLE;
    VmaAllocation  vbo_alloc_   = VK_NULL_HANDLE;
    VkDeviceSize   vbo_size_    = 0;

    VkBuffer       ibo_         = VK_NULL_HANDLE;
    VmaAllocation  ibo_alloc_   = VK_NULL_HANDLE;
    VkDeviceSize   ibo_size_    = 0;
};

}  // namespace snt::ui
