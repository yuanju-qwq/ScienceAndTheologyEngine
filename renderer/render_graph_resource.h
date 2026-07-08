// Render graph resource handle: texture or buffer used as pass attachment.
//
// Resources are referenced by handle (not pointer) so the graph can track
// lifetime and state across passes without exposing Vulkan objects to
// upper layers.
//
// P2.3: physical resource descriptors added so TransientPool can allocate
// the right VkImage/VkBuffer on demand. Resources come in two flavors:
//   - Transient: created via RenderGraph::create_texture/create_buffer,
//     backed by TransientPool, lifetime = single frame.
//   - External:  registered via RenderGraph::import_texture, backed by
//     a caller-provided VkImage/VkImageView (e.g. swapchain image, depth
//     buffer owned by VulkanFrame/VulkanDepth).
//
// Reference: vkb-framework ResourceCache uses similar handle-based access;
// Granite uses integer handles with a slot map.

#pragma once

#include <cstdint>

namespace snt::renderer {

// Resource type tag.
enum class ResourceType : uint8_t {
    kInvalid = 0,
    kTexture,
    kBuffer,
};

// Resource usage flag — what a pass wants to do with the resource.
// The graph uses these to insert barriers (P2.3).
// Bitmask-style; helper operators will be added when P2.3 needs them.
enum class ResourceUsage : uint16_t {
    kNone = 0,
    kShaderRead = 1 << 0,
    kColorOutput = 1 << 1,
    kDepthOutput = 1 << 2,
    kTransferSrc = 1 << 3,
    kTransferDst = 1 << 4,
};

// Texture descriptor: enough info for TransientPool to allocate a VkImage.
struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;        // VkFormat as uint32_t (keep header Vulkan-free)
    uint32_t usage_flags = 0;   // VkImageUsageFlags as uint32_t
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
};

// Buffer descriptor: enough info for TransientPool to allocate a VkBuffer.
struct BufferDesc {
    uint64_t size = 0;
    uint32_t usage_flags = 0;   // VkBufferUsageFlags as uint32_t
};

// Opaque resource handle. Created by RenderGraph, passed to passes.
// Backed by a slot in the graph's resource pool.
struct RenderResource {
    uint32_t id = kInvalidId;
    ResourceType type = ResourceType::kInvalid;

    bool valid() const { return id != kInvalidId; }

    static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;
};

}  // namespace snt::renderer
