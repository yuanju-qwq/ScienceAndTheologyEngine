// Vulkan Frame implementation.
// P2.D: owns swapchain sync (fences + semaphores + acquire/present).
// Recording is owned by RenderGraph / CommandContext.

#define SNT_LOG_CHANNEL "render_backend"
#include "core/log.h"

#include "vulkan_frame.h"
#include "vulkan_device.h"
#include "vulkan_swapchain.h"

#include <volk.h>

// VK_KHR_swapchain_maintenance1 extension name macro may be missing from
// older Vulkan headers, even when the struct is defined. Define it manually.
#ifndef VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME
#define VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME "VK_KHR_swapchain_maintenance1"
#endif

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanFrame::~VulkanFrame() {
    destroy();
}

snt::core::Expected<void> VulkanFrame::init(VulkanDevice& device, uint32_t swapchain_image_count) {
    device_ = &device;

    // --- Create command pool ---
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.graphics_family(),
    };

    if (vkCreateCommandPool(device_->logical(), &pool_info, nullptr,
                            &command_pool_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandPoolFailed,
                                "vkCreateCommandPool failed"};
    }

    // --- Allocate command buffers (one per frame in flight) ---
    command_buffers_.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(command_buffers_.size()),
    };

    if (vkAllocateCommandBuffers(device_->logical(), &alloc_info,
                                 command_buffers_.data()) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                "vkAllocateCommandBuffers failed"};
    }

    // --- Create per-frame-in-flight acquire semaphores ---
    // image_available_[current_frame_] is signaled by vkAcquireNextImageKHR
    // and consumed by vkQueueSubmit's wait. Safe to reuse per frame slot
    // because in_flight_fences_ guarantees the previous submit is complete.
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkSemaphoreCreateInfo sem_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        if (vkCreateSemaphore(device_->logical(), &sem_info, nullptr,
                              &image_available_[i]) != VK_SUCCESS) {
            return snt::core::Error{snt::core::ErrorCode::kVulkanFrameInitFailed,
                                    "Failed to create acquire semaphores"};
        }
    }

    // --- Create per-swapchain-image render-done semaphores ---
    // render_finished_[image_index] is signaled by vkQueueSubmit and waited
    // on by vkQueuePresentKHR. Indexed by image_index (not current_frame_)
    // because without VK_KHR_swapchain_maintenance1 the present operation
    // holds the semaphore until that image is re-acquired. A per-frame
    // semaphore would be reused while still in use by a previous present.
    render_finished_.resize(swapchain_image_count);
    for (uint32_t i = 0; i < swapchain_image_count; ++i) {
        VkSemaphoreCreateInfo sem_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        if (vkCreateSemaphore(device_->logical(), &sem_info, nullptr,
                              &render_finished_[i]) != VK_SUCCESS) {
            return snt::core::Error{snt::core::ErrorCode::kVulkanFrameInitFailed,
                                    "Failed to create render semaphores"};
        }
    }

    // --- Create per-swapchain-image present fences ---
    // Signaled by vkQueuePresentKHR via VK_KHR_swapchain_maintenance1.
    // These survive swapchain recreation and let us wait for present
    // completion before destroying an old swapchain.
    // Issue1 fix: only allocate when the extension is actually enabled on
    // the device. Without the extension, VkSwapchainPresentFenceInfoEXT
    // must not be used (undefined behavior); draw_frame() leaves pNext=null.
    if (device_->has_swapchain_maintenance1()) {
        present_fences_.resize(swapchain_image_count);
        for (uint32_t i = 0; i < swapchain_image_count; ++i) {
            VkFenceCreateInfo fence_info{
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,  // start signaled
            };
            if (vkCreateFence(device_->logical(), &fence_info, nullptr,
                              &present_fences_[i]) != VK_SUCCESS) {
                return snt::core::Error{snt::core::ErrorCode::kVulkanFrameInitFailed,
                                        "Failed to create present fences"};
            }
        }
    }

    // --- Create per-frame-in-flight fences ---
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,  // start signaled so first
                                                    // vkWaitForFences doesn't hang
        };
        if (vkCreateFence(device_->logical(), &fence_info, nullptr,
                          &in_flight_fences_[i]) != VK_SUCCESS) {
            return snt::core::Error{snt::core::ErrorCode::kVulkanFrameInitFailed,
                                    "Failed to create fences"};
        }
    }

    SNT_LOG_INFO("Frame resources created (%u frames in flight, %u swapchain images)",
                 kMaxFramesInFlight, swapchain_image_count);
    return {};
}

void VulkanFrame::destroy() {
    if (!device_ || device_->logical() == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (in_flight_fences_[i]) {
            vkDestroyFence(device_->logical(), in_flight_fences_[i], nullptr);
            in_flight_fences_[i] = VK_NULL_HANDLE;
        }
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (image_available_[i]) vkDestroySemaphore(device_->logical(), image_available_[i], nullptr);
        image_available_[i] = VK_NULL_HANDLE;
    }

    for (auto sem : render_finished_) {
        if (sem) vkDestroySemaphore(device_->logical(), sem, nullptr);
    }
    render_finished_.clear();

    for (auto fence : present_fences_) {
        if (fence) vkDestroyFence(device_->logical(), fence, nullptr);
    }
    present_fences_.clear();

    // pool automatically frees all command buffers allocated from it. The
    // vkDestroyCommandPool() call below handles cleanup; an explicit
    // vkFreeCommandBuffers() is therefore unnecessary here.
    command_buffers_.clear();

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_->logical(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Frame API (P2.D): RenderGraph owns recording, VulkanFrame owns sync.
// ---------------------------------------------------------------------------

VulkanFrame::FrameResult VulkanFrame::begin_frame(VulkanDevice& device,
                                                   VulkanSwapchain& swapchain,
                                                   uint32_t* out_image_index) {
    // Wait for the previous frame using this slot, then reset the fence.
    vkWaitForFences(device.logical(), 1, &in_flight_fences_[current_frame_],
                    VK_TRUE, UINT64_MAX);
    vkResetFences(device.logical(), 1, &in_flight_fences_[current_frame_]);

    // Acquire the next swapchain image. The acquire semaphore is signaled
    // by vkAcquireNextImageKHR and later consumed by end_frame's submit.
    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(
        device.logical(), swapchain.handle(), UINT64_MAX,
        image_available_[current_frame_], VK_NULL_HANDLE, &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return FrameResult::kResized;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        SNT_LOG_ERROR("begin_frame: acquire failed: %d", result);
        return FrameResult::kError;
    }

    *out_image_index = image_index;
    return FrameResult::kOk;
}

VulkanFrame::FrameResult VulkanFrame::end_frame(VulkanDevice& device,
                                                 VulkanSwapchain& swapchain,
                                                 uint32_t image_index,
                                                 const VkCommandBuffer* cmd_buffers,
                                                 uint32_t cmd_buffer_count) {
    // Submit: wait on the acquire semaphore at color attachment output stage,
    // signal the render-done semaphore for this swapchain image.
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &image_available_[current_frame_],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = cmd_buffer_count,
        .pCommandBuffers = cmd_buffers,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &render_finished_[image_index],
    };

    if (vkQueueSubmit(device.graphics_queue(), 1, &submit_info,
                      in_flight_fences_[current_frame_]) != VK_SUCCESS) {
        SNT_LOG_ERROR("end_frame: vkQueueSubmit failed");
        return FrameResult::kError;
    }

    // Present. Conditionally attach the present fence when
    // VK_KHR_swapchain_maintenance1 is available.
    VkSwapchainPresentFenceInfoEXT present_fence_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .swapchainCount = 1,
        .pFences = nullptr,
    };
    const bool use_present_fence = device_->has_swapchain_maintenance1()
                                   && image_index < present_fences_.size();
    if (use_present_fence) {
        present_fence_info.pFences = &present_fences_[image_index];
    }

    VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = use_present_fence ? &present_fence_info : nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_finished_[image_index],
        .swapchainCount = 1,
        .pSwapchains = &(const VkSwapchainKHR&)swapchain.handle(),
        .pImageIndices = &image_index,
    };

    VkResult result = vkQueuePresentKHR(device.present_queue(), &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Still advance the frame slot so the next begin_frame uses the
        // correct fence.
        current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
        return FrameResult::kResized;
    }
    if (result != VK_SUCCESS) {
        SNT_LOG_ERROR("end_frame: present failed: %d", result);
        current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
        return FrameResult::kError;
    }

    current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
    return FrameResult::kOk;
}

}  // namespace snt::render_backend
