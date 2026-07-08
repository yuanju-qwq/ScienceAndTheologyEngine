// TransientPool implementation.
//
// P2.3: uses VMA_ALLOCATION_CREATE_TRANSIENT_BIT for short-lived resources.
// Resources are allocated on demand and freed en masse in reset().

#define SNT_LOG_CHANNEL "renderer"
#include "core/log.h"

#include "renderer/transient_pool.h"
#include "renderer/render_graph_resource.h"

#include <volk.h>

#include <format>

namespace snt::renderer {

TransientPool::~TransientPool() {
    destroy();
}

snt::core::Expected<void> TransientPool::init(VmaAllocator allocator) {
    if (allocator == VK_NULL_HANDLE) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "TransientPool::init: null allocator"};
    }
    allocator_ = allocator;
    return {};
}

void TransientPool::destroy() {
    reset();
    allocator_ = VK_NULL_HANDLE;
}

snt::core::Expected<void> TransientPool::create_texture(const TextureDesc& desc,
                                   VkImage* out_image,
                                   VkImageView* out_view,
                                   VmaAllocation* out_allocation) {
    if (allocator_ == VK_NULL_HANDLE) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "TransientPool::create_texture: not initialized"};
    }

    // --- Create VkImage via VMA ---
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = static_cast<VkFormat>(desc.format);
    image_info.extent = {desc.width, desc.height, 1};
    image_info.mipLevels = desc.mip_levels;
    image_info.arrayLayers = desc.array_layers;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = desc.usage_flags;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    // VMA_AUTO + DEDICATED_MEMORY is the recommended usage for short-lived
    // transient resources (VMA 3.x; the old TRANSIENT_BIT was removed).
    // Dedicated allocation avoids suballocation overhead for resources
    // that are created + destroyed every frame.
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    TextureAlloc a{};
    VkResult result = vmaCreateImage(allocator_, &image_info, &alloc_info,
                                     &a.image, &a.allocation, nullptr);
    if (result != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanBufferInitFailed,
                                std::format("TransientPool: vmaCreateImage failed: {}", static_cast<int>(result))};
    }

    // --- Create VkImageView ---
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = a.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = static_cast<VkFormat>(desc.format);
    view_info.subresourceRange.aspectMask = 0;
    // Derive aspect mask from format. Depth/stencil formats need the
    // depth aspect; color formats need the color aspect.
    VkFormat fmt = static_cast<VkFormat>(desc.format);
    if (fmt >= VK_FORMAT_D16_UNORM && fmt <= VK_FORMAT_D32_SFLOAT_S8_UINT) {
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = desc.mip_levels;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = desc.array_layers;

    // Grab the VkDevice from VMA to create the view.
    VmaAllocatorInfo alloc_info_out{};
    vmaGetAllocatorInfo(allocator_, &alloc_info_out);
    result = vkCreateImageView(alloc_info_out.device, &view_info, nullptr,
                               &a.view);
    if (result != VK_SUCCESS) {
        vmaDestroyImage(allocator_, a.image, a.allocation);
        return snt::core::Error{snt::core::ErrorCode::kVulkanBufferInitFailed,
                                std::format("TransientPool: vkCreateImageView failed: {}", static_cast<int>(result))};
    }

    *out_image = a.image;
    *out_view = a.view;
    *out_allocation = a.allocation;

    textures_.push_back(a);
    return {};
}

snt::core::Expected<void> TransientPool::create_buffer(const BufferDesc& desc,
                                  VkBuffer* out_buffer,
                                  VmaAllocation* out_allocation) {
    if (allocator_ == VK_NULL_HANDLE) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "TransientPool::create_buffer: not initialized"};
    }

    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = desc.size;
    buf_info.usage = desc.usage_flags;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    BufferAlloc a{};
    VkResult result = vmaCreateBuffer(allocator_, &buf_info, &alloc_info,
                                      &a.buffer, &a.allocation, nullptr);
    if (result != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanBufferInitFailed,
                                std::format("TransientPool: vmaCreateBuffer failed: {}", static_cast<int>(result))};
    }

    *out_buffer = a.buffer;
    *out_allocation = a.allocation;

    buffers_.push_back(a);
    return {};
}

void TransientPool::reset() {
    if (allocator_ == VK_NULL_HANDLE) return;

    // Free textures (view first, then image+allocation via VMA).
    VmaAllocatorInfo info{};
    vmaGetAllocatorInfo(allocator_, &info);
    for (auto& a : textures_) {
        if (a.view != VK_NULL_HANDLE) {
            vkDestroyImageView(info.device, a.view, nullptr);
        }
        if (a.image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, a.image, a.allocation);
        }
    }
    textures_.clear();

    for (auto& a : buffers_) {
        if (a.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, a.buffer, a.allocation);
        }
    }
    buffers_.clear();
}

}  // namespace snt::renderer
