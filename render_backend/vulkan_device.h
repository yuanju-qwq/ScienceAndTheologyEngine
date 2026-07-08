// Vulkan Device — physical device selection + logical device + queues.
//
// P1.3 responsibilities:
//   - Pick best physical device (prefer discrete GPU, fallback integrated)
//   - Find graphics + present queue families
//   - Create logical device with VK_KHR_swapchain extension
//   - Load device-level function pointers via volkLoadDevice
//
// Usage:
//   VulkanDevice device;
//   if (!device.init(instance, surface)) { /* fail */ }
//   // device.physical(), device.logical(), device.graphics_queue() usable.

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

namespace snt::render_backend {

class VulkanDevice {
public:
    VulkanDevice() = default;
    ~VulkanDevice();

    // Non-copyable; RAII.
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    // Select physical device + create logical device.
    // `surface` is the VkSurfaceKHR from the platform window.
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> init(VkInstance instance, VkSurfaceKHR surface);

    // Destroy logical device. Called automatically by destructor.
    void destroy();

    // Wait for all device work to complete (idle).
    void wait_idle() const;

    // VMA allocator (created during init, used by VulkanBuffer etc.).
    VmaAllocator vma_allocator() const { return vma_allocator_; }

    VkPhysicalDevice physical() const { return physical_; }
    VkDevice logical() const { return device_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkQueue graphics_queue() const { return graphics_queue_; }
    VkQueue present_queue() const { return present_queue_; }
    uint32_t graphics_family() const { return graphics_family_; }
    uint32_t present_family() const { return present_family_; }

    // Whether VK_KHR_swapchain_maintenance1 was enabled on the logical device.
    // When true, vkQueuePresentKHR can signal a VkFence (via
    // VkSwapchainPresentFenceInfoEXT) which survives swapchain recreation.
    // VulkanFrame queries this to conditionally attach the present fence.
    bool has_swapchain_maintenance1() const { return has_swapchain_maintenance1_; }

    // Swapchain support details queried during device selection.
    struct SwapchainSupport {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> present_modes;
    };
    SwapchainSupport query_swapchain_support() const;

private:
    // Rate a physical device (higher = better). 0 means unsuitable.
    int rate_device(VkPhysicalDevice dev, VkSurfaceKHR surface) const;

    // Find queue family indices for graphics + present.
    bool find_queue_families(VkPhysicalDevice dev, VkSurfaceKHR surface);

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;  // stored for query_swapchain_support()
    VmaAllocator vma_allocator_ = VK_NULL_HANDLE;  // VMA allocator for buffers/images
    uint32_t graphics_family_ = UINT32_MAX;
    uint32_t present_family_ = UINT32_MAX;
    bool has_swapchain_maintenance1_ = false;  // set during init()
};

}  // namespace snt::render_backend
