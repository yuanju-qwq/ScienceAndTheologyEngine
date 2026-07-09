// MuiRenderer — Vulkan rendering backend for MUI (immediate-mode UI).
//
// Responsibilities:
//   - Bake an ASCII font atlas (32..126) via stb_truetype into a single
//     R8_UNORM texture at init time.
//   - Create a UI graphics pipeline: pos2D+uv vertex layout, orthographic
//     projection UBO, alpha blending, no depth test/write (UI draws on top).
//   - Per frame: receive a DrawData (vertices + indices) from MuiContext,
//     upload to a staging buffer, and record draw calls into the active
//     command buffer.
//
// Integration: RenderSystem records UI draws at the END of its forward
// pass callback (same render pass scope, same command buffer). The UI
// pipeline switches off depth write and enables alpha blend so text
// composites over the 3D scene.
//
// Layering: sits in ui/, depends on render_backend (VulkanDevice, pipeline,
// buffer, descriptor) + stb_truetype. MuiContext (ui/mui.h) produces
// DrawData; MuiRenderer consumes it.

#pragma once

#include "mui.h"  // UiVertex, UiDrawData, GlyphInfo (Vulkan-free types)

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

    // Initialize: bake font atlas, create texture + pipeline + descriptors.
    // `font_path` is a TTF file path (e.g. "C:\\Windows\\Fonts\\arial.ttf").
    // `color_format` is the swapchain image format for the pipeline.
    snt::core::Expected<void> init(snt::render_backend::VulkanDevice& device,
                                   VkFormat color_format,
                                   const std::string& font_path,
                                   float font_size_px = 16.0f);

    void destroy();

    // Update the orthographic projection UBO for the current swapchain extent.
    // Call once per frame before render().
    void update_ortho(uint32_t fb_width, uint32_t fb_height);

    // Record UI draw calls into `cmd`. Uploads `draw_data` vertices/indices
    // to a CPU-visible buffer and draws them with the UI pipeline.
    void render(VkCommandBuffer cmd, const UiDrawData& draw_data);

    // Glyph lookup for MuiContext text layout (ASCII 32..126).
    // Returns nullptr for characters outside the baked range.
    const GlyphInfo* glyph(char c) const;

    // Font metrics for text layout.
    float font_size() const { return font_size_; }
    float line_height() const { return line_height_; }

private:
    // Bake the font atlas: load TTF, rasterize ASCII 32..126 into a bitmap,
    // create Vulkan texture + view + sampler.
    snt::core::Expected<void> bake_font_atlas(const std::string& font_path);

    // Create the UI descriptor set layout + pool + set (UBO + sampler).
    snt::core::Expected<void> create_descriptors();

    // Create the UI graphics pipeline.
    snt::core::Expected<void> create_pipeline(VkFormat color_format);

    snt::render_backend::VulkanDevice*              device_ = nullptr;

    // Font atlas texture.
    VkImage        atlas_image_       = VK_NULL_HANDLE;
    VkImageView    atlas_view_        = VK_NULL_HANDLE;
    VmaAllocation  atlas_allocation_  = VK_NULL_HANDLE;
    VkSampler      atlas_sampler_     = VK_NULL_HANDLE;
    VkDeviceMemory atlas_staging_     = VK_NULL_HANDLE;  // staging buffer for upload

    // Glyph lookup table (indexed by char code 32..126).
    static constexpr int kFirstChar = 32;
    static constexpr int kLastChar  = 126;
    GlyphInfo glyphs_[kLastChar - kFirstChar + 1] = {};

    float font_size_   = 16.0f;
    float line_height_ = 20.0f;

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
