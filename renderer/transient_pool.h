// TransientPool: per-frame transient resource allocator backed by VMA.
//
// P2.3 design (decision A: VMA transient bit):
//   - Each frame, passes declare transient textures/buffers via
//     RenderGraph::create_texture/create_buffer.
//   - On first use (during execute), the pool allocates the actual VkImage/
//     VkBuffer via vmaCreateImage/vmaCreateBuffer with
//     VMA_ALLOCATION_CREATE_TRANSIENT_BIT.
//   - On reset() (frame end), all transient resources are freed.
//
// This is simpler than a full virtual allocator + memory aliasing scheme
// (Granite style) and sufficient for P2/P3 pass counts. P4 may upgrade to
// aliasing if memory pressure becomes a problem.
//
// VMA's internal pool keeps per-allocation overhead low; the transient bit
// hints to VMA that these allocations are short-lived so it can use a
// faster allocation strategy.

#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/expected.h"  // Expected<void> for init / create_texture / create_buffer

namespace snt::renderer {

struct RenderResource;
struct TextureDesc;
struct BufferDesc;

class TransientPool {
public:
    TransientPool() = default;
    ~TransientPool();

    TransientPool(const TransientPool&) = delete;
    TransientPool& operator=(const TransientPool&) = delete;

    // Initialize with the VMA allocator. Does not take ownership.
    snt::core::Expected<void> init(VmaAllocator allocator);

    // Release all allocations. Idempotent.
    void destroy();

    // Allocate (or return cached) VkImage for a transient texture resource.
    // The image is created with the format/usage from `desc`. On success,
    // `out_image` + `out_view` + `out_allocation` are set.
    // Returns an Error on VMA/Vulkan failure.
    snt::core::Expected<void> create_texture(const TextureDesc& desc, VkImage* out_image,
                        VkImageView* out_view, VmaAllocation* out_allocation);

    // Allocate (or return cached) VkBuffer for a transient buffer resource.
    snt::core::Expected<void> create_buffer(const BufferDesc& desc, VkBuffer* out_buffer,
                       VmaAllocation* out_allocation);

    // Release all transient resources. Called by RenderGraph::reset().
    void reset();

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // Track allocations so reset() can free them all.
    struct TextureAlloc {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
    };
    struct BufferAlloc {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
    };
    std::vector<TextureAlloc> textures_;
    std::vector<BufferAlloc>  buffers_;
};

}  // namespace snt::renderer
