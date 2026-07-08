// CommandContext implementation.

#define SNT_LOG_CHANNEL "render_backend"
#include "core/log.h"

#include "render_backend/command_context.h"

#include "render_backend/vulkan_device.h"

#include <volk.h>

namespace snt::render_backend {

CommandContext::~CommandContext() {
    reset();
}

snt::core::Expected<void> CommandContext::begin_recording(VulkanDevice& device, VkCommandPool pool) {
    // If we already hold a buffer, recycle it (vkResetCommandBuffer rather
    // than free+allocate). This keeps the per-pass allocation cost at zero
    // after the first frame.
    if (command_buffer_ == VK_NULL_HANDLE) {
        VkCommandBufferAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(device.logical(), &alloc_info,
                                     &command_buffer_) != VK_SUCCESS) {
            return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                    "CommandContext: vkAllocateCommandBuffers failed"};
        }
        owns_buffer_ = true;
    }

    device_ = &device;
    command_pool_ = pool;

    // Reset to a clean state before re-recording.
    vkResetCommandBuffer(command_buffer_, 0);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        // One-time submit is appropriate for per-frame passes recorded
        // fresh each frame by the render graph.
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(command_buffer_, &begin_info) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                "CommandContext: vkBeginCommandBuffer failed"};
    }

    recording_ = true;
    return {};
}

void CommandContext::end_recording() {
    if (!recording_) return;
    if (vkEndCommandBuffer(command_buffer_) != VK_SUCCESS) {
        SNT_LOG_ERROR("CommandContext: vkEndCommandBuffer failed");
    }
    recording_ = false;
}

// P2.3 (option B): dynamic rendering helpers.
// vkCmdBeginRendering / vkCmdEndRendering are Vulkan 1.3 core entry points
// (no extension needed). The caller builds VkRenderingInfo; we just forward.
void CommandContext::begin_rendering(const VkRenderingInfo& rendering_info) {
    if (!recording_ || command_buffer_ == VK_NULL_HANDLE) return;
    vkCmdBeginRendering(command_buffer_, &rendering_info);
}

void CommandContext::end_rendering() {
    if (!recording_ || command_buffer_ == VK_NULL_HANDLE) return;
    vkCmdEndRendering(command_buffer_);
}

void CommandContext::reset() {
    // Free the allocated command buffer back to the pool. Safe to call
    // even if begin_recording was never called.
    if (owns_buffer_ && command_buffer_ != VK_NULL_HANDLE && device_ &&
        command_pool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_->logical(), command_pool_, 1,
                             &command_buffer_);
    }
    command_buffer_ = VK_NULL_HANDLE;
    command_pool_ = VK_NULL_HANDLE;
    device_ = nullptr;
    recording_ = false;
    owns_buffer_ = false;
}

}  // namespace snt::render_backend
