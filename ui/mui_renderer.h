// MuiRenderer — Vulkan rendering backend for retained MUI draw batches.
//
// Responsibilities:
//   - Own dynamic RGBA Unicode-glyph and UI-image atlas textures synchronized
//     from UiDrawData.
//   - Create a UI graphics pipeline: pos2D+uv vertex layout, orthographic
//     projection UBO, alpha blending, no depth test/write (UI draws on top).
//   - Per frame: receive UiDrawData from retained MUI / Arc2D, upload to a
//     staging buffer, and record clipped atlas-bound draw calls.
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
namespace snt::core { class RuntimePathResolver; }

namespace snt::ui {

class MuiRenderer {
public:
    MuiRenderer() = default;
    ~MuiRenderer();

    MuiRenderer(const MuiRenderer&) = delete;
    MuiRenderer& operator=(const MuiRenderer&) = delete;

    // Initialize dynamic atlas textures, pipeline and descriptors.
    // `paths` is borrowed only during init for engine-owned shader lookup.
    // `color_format` is the swapchain image format for the pipeline.
    snt::core::Expected<void> init(snt::render_backend::VulkanDevice& device,
                                   VkFormat color_format,
                                   const snt::core::RuntimePathResolver& paths);

    void destroy();

    // Update the orthographic projection UBO for the current swapchain extent.
    // Call once per frame before render().
    void update_ortho(uint32_t fb_width, uint32_t fb_height);

    // Synchronize the frame's glyph and image atlases outside a render pass.
    // The engine invokes this after retained UI construction and before it
    // records the UI pass, so uploads never race active Vulkan rendering.
    snt::core::Expected<void> synchronize_atlases(const UiDrawData& draw_data);

    // Record UI draw calls into `cmd`. Uploads `draw_data` vertices/indices
    // to a CPU-visible buffer and draws them with the UI pipeline.
    void render(VkCommandBuffer cmd, const UiDrawData& draw_data);

private:
    struct AtlasResource {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t uploaded_revision = 0;
        const UiRasterAtlas* uploaded_atlas = nullptr;
    };

    snt::core::Expected<void> create_atlas_texture(AtlasResource& resource,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    const char* label);
    snt::core::Expected<void> upload_atlas(AtlasResource& resource,
                                           const UiRasterAtlas& atlas,
                                           const char* label);

    // Create the UI descriptor set layout + pool + two atlas sets (UBO + sampler).
    snt::core::Expected<void> create_descriptors();

    // Create the UI graphics pipeline.
    snt::core::Expected<void> create_pipeline(
        VkFormat color_format,
        const snt::core::RuntimePathResolver& paths);

    snt::render_backend::VulkanDevice*              device_ = nullptr;

    AtlasResource glyph_atlas_;
    AtlasResource image_atlas_;

    // Pipeline + descriptor.
    VkPipelineLayout      pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline            pipeline_        = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout_     = VK_NULL_HANDLE;
    VkDescriptorPool      desc_pool_       = VK_NULL_HANDLE;
    VkDescriptorSet       glyph_desc_set_  = VK_NULL_HANDLE;
    VkDescriptorSet       image_desc_set_  = VK_NULL_HANDLE;

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

    uint32_t framebuffer_width_ = 0;
    uint32_t framebuffer_height_ = 0;
};

}  // namespace snt::ui
