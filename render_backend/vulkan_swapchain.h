// Vulkan Swapchain — creates swapchain + image views, handles recreation.
//
// P1.3 responsibilities:
//   - Choose surface format (prefer B8G8R8A8_SRGB + SRGB_NONLINEAR)
//   - Choose present mode (prefer Mailbox for low-latency, fallback FIFO)
//   - Choose extent (clamp to window size + device limits)
//   - Create swapchain + retrieve images + create image views
//   - recreate() on window resize (destroy old, create new)
//
// P1.4 will add: render pass, framebuffers, command buffers, present loop.

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace snt::platform { struct WindowSize; }

namespace snt::render_backend {

class VulkanDevice;

class VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain();

    // Non-copyable; RAII.
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // Create swapchain. `device` provides physical/logical device + queues.
    // `width`/`height` are the drawable extent (window client area).
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> init(VulkanDevice& device, uint32_t width, uint32_t height);

    // Destroy swapchain + image views. Called automatically by destructor.
    void destroy();

    // Recreate swapchain for a new window size. Destroys old swapchain first.
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> recreate(uint32_t width, uint32_t height);

    VkSwapchainKHR handle() const { return swapchain_; }
    VkFormat image_format() const { return image_format_; }
    VkExtent2D extent() const { return extent_; }
    const std::vector<VkImage>& images() const { return images_; }
    const std::vector<VkImageView>& image_views() const { return image_views_; }

private:
    // Choose best surface format (prefer SRGB).
    VkSurfaceFormatKHR choose_format(
        const std::vector<VkSurfaceFormatKHR>& available) const;

    // Choose best present mode (prefer Mailbox, fallback FIFO).
    VkPresentModeKHR choose_present_mode(
        const std::vector<VkPresentModeKHR>& available) const;

    // Choose extent: use window size clamped to device limits.
    VkExtent2D choose_extent(
        const VkSurfaceCapabilitiesKHR& caps,
        uint32_t width, uint32_t height) const;

    VulkanDevice* device_ = nullptr;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat image_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ = {0, 0};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
};

}  // namespace snt::render_backend
