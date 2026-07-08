// Vulkan Swapchain implementation.

#define SNT_LOG_CHANNEL "render_backend"
#include "core/log.h"

#include "vulkan_swapchain.h"
#include "vulkan_device.h"

#include <volk.h>

#include <algorithm>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Helpers: choose best format / present mode / extent
// ---------------------------------------------------------------------------

VkSurfaceFormatKHR VulkanSwapchain::choose_format(
    const std::vector<VkSurfaceFormatKHR>& available) const {
    // Prefer B8G8R8A8_SRGB with SRGB_NONLINEAR color space.
    for (const auto& f : available) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    // Fallback: first available.
    return available.front();
}

VkPresentModeKHR VulkanSwapchain::choose_present_mode(
    const std::vector<VkPresentModeKHR>& available) const {
    // Prefer Mailbox (low-latency, triple-buffered, vsync-on).
    // FIFO is always available (vsync-on, no tearing) but higher latency.
    for (auto m : available) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchain::choose_extent(
    const VkSurfaceCapabilitiesKHR& caps,
    uint32_t width, uint32_t height) const {
    // If extent is UINT32_MAX, the platform lets us pick; otherwise use
    // the fixed extent. Clamp to the device's min/max bounds.
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    VkExtent2D actual = {
        .width = std::max(caps.minImageExtent.width,
                          std::min(caps.maxImageExtent.width, width)),
        .height = std::max(caps.minImageExtent.height,
                           std::min(caps.maxImageExtent.height, height)),
    };
    return actual;
}

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanSwapchain::~VulkanSwapchain() {
    destroy();
}

snt::core::Expected<void> VulkanSwapchain::init(VulkanDevice& device, uint32_t width, uint32_t height) {
    device_ = &device;
    return recreate(width, height);
}

void VulkanSwapchain::destroy() {
    if (!device_ || device_->logical() == VK_NULL_HANDLE) return;

    // Image views are created by us; images are owned by the swapchain.
    for (auto view : image_views_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_->logical(), view, nullptr);
        }
    }
    image_views_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_->logical(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    images_.clear();
}

snt::core::Expected<void> VulkanSwapchain::recreate(uint32_t width, uint32_t height) {
    if (!device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "recreate() called before init()"};
    }

    // --- Query swapchain support ---
    VulkanDevice::SwapchainSupport support = device_->query_swapchain_support();
    if (support.formats.empty() || support.present_modes.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kNoSurfaceFormats,
                                "Swapchain: no formats/modes"};
    }

    VkSurfaceFormatKHR format = choose_format(support.formats);
    VkPresentModeKHR present_mode = choose_present_mode(support.present_modes);
    VkExtent2D extent = choose_extent(support.capabilities, width, height);

    // --- Image count: request at least minImageCount + 1 for triple buffering ---
    uint32_t image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        image_count > support.capabilities.maxImageCount) {
        image_count = support.capabilities.maxImageCount;
    }

    // --- Create swapchain ---
    VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = device_->surface(),
        .minImageCount = image_count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,  // single queue family
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = support.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = swapchain_,  // for recreation: hand old swapchain to driver
    };

    // If graphics + present families differ, use CONCURRENT sharing.
    if (device_->graphics_family() != device_->present_family()) {
        uint32_t families[] = {device_->graphics_family(),
                               device_->present_family()};
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = families;
    }

    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device_->logical(), &create_info, nullptr,
                             &new_swapchain) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanSwapchainInitFailed,
                                "vkCreateSwapchainKHR failed"};
    }

    // Old swapchain (if any) can now be destroyed.
    if (swapchain_ != VK_NULL_HANDLE) {
        for (auto view : image_views_) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device_->logical(), view, nullptr);
            }
        }
        image_views_.clear();
        vkDestroySwapchainKHR(device_->logical(), swapchain_, nullptr);
    }
    swapchain_ = new_swapchain;

    // --- Retrieve swapchain images ---
    uint32_t img_count = 0;
    vkGetSwapchainImagesKHR(device_->logical(), swapchain_, &img_count, nullptr);
    images_.resize(img_count);
    vkGetSwapchainImagesKHR(device_->logical(), swapchain_, &img_count, images_.data());

    image_format_ = format.format;
    extent_ = extent;

    // --- Create image views ---
    image_views_.resize(img_count);
    for (uint32_t i = 0; i < img_count; ++i) {
        VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images_[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image_format_,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        if (vkCreateImageView(device_->logical(), &view_info, nullptr,
                              &image_views_[i]) != VK_SUCCESS) {
            return snt::core::Error{snt::core::ErrorCode::kVulkanSwapchainInitFailed,
                                    "vkCreateImageView failed"};
        }
    }

    SNT_LOG_INFO("Swapchain: %ux%u, %u images, fmt=%u",
                 extent_.width, extent_.height, img_count, image_format_);
    return {};
}

}  // namespace snt::render_backend
