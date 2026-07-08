// Vulkan Depth Buffer — depth attachment for depth testing.
//
// P1.5: creates a depth image + view matching the swapchain extent.
// Used by the render pass (depth attachment) and pipeline (depth test).

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace snt::render_backend {

class VulkanDevice;
class VulkanSwapchain;

class VulkanDepth {
public:
    VulkanDepth() = default;
    ~VulkanDepth();

    VulkanDepth(const VulkanDepth&) = delete;
    VulkanDepth& operator=(const VulkanDepth&) = delete;

    // Create depth image + view for the given swapchain extent.
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> init(VulkanDevice& device, VulkanSwapchain& swapchain);

    void destroy();

    // Recreate depth image when swapchain is resized.
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> recreate(VulkanSwapchain& swapchain);

    VkImage image() const { return depth_image_; }
    VkImageView view() const { return depth_view_; }
    VkFormat format() const { return depth_format_; }

private:
    // Find a supported depth format (prefer D32_SFLOAT, fallback D24_UNORM).
    VkFormat find_depth_format();

    VulkanDevice* device_ = nullptr;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
};

}  // namespace snt::render_backend
