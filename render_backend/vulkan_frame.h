// Vulkan Frame — per-frame command buffer + sync primitives + draw logic.
//
// P1.5: mesh rendering with MVP UBO + depth testing.
// P1.4 was hardcoded triangle; P1.5 draws a VulkanMesh with descriptor sets.
//
// MAX_FRAMES_IN_FLIGHT = 2: allows CPU to record frame N+1 while GPU
// renders frame N. Each frame has its own command buffer + semaphores +
// fence to avoid synchronization across frames.

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace snt::render_backend {

class VulkanDevice;
class VulkanSwapchain;
class VulkanPipeline;
class VulkanDescriptor;
class VulkanMesh;
struct UniformBufferObject;

class VulkanFrame {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    VulkanFrame() = default;
    ~VulkanFrame();

    VulkanFrame(const VulkanFrame&) = delete;
    VulkanFrame& operator=(const VulkanFrame&) = delete;

    // Create command pool + command buffers + semaphores + fences.
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> init(VulkanDevice& device, uint32_t swapchain_image_count);

    void destroy();

    // --- Frame API (P2.D) ---
    // VulkanFrame owns swapchain sync (fences + semaphores + acquire/present).
    // Recording is owned by RenderGraph / CommandContext.
    //
    // begin_frame: wait for the previous frame using this slot, reset fence,
    //   acquire the next swapchain image. On success, `out_image_index`
    //   receives the acquired image index.
    // Returns:
    //   kOk       — image acquired, ready to record + submit
    //   kResized  — swapchain out of date, caller must recreate
    //   kError    — unrecoverable failure
    enum class FrameResult { kOk, kResized, kError };
    FrameResult begin_frame(VulkanDevice& device,
                            VulkanSwapchain& swapchain,
                            uint32_t* out_image_index);

    // end_frame: submit recorded command buffers to the graphics queue
    //   (waiting on the acquire semaphore, signaling the render-done
    //   semaphore) then present.
    //   `image_index` must be the value returned by begin_frame().
    //   `cmd_buffers` / `cmd_buffer_count` is the array of recorded command
    //   buffers to submit (typically all passes from RenderGraph).
    // Returns kOk on success, kResized if present reported out-of-date,
    //   kError on submit/present failure.
    FrameResult end_frame(VulkanDevice& device,
                          VulkanSwapchain& swapchain,
                          uint32_t image_index,
                          const VkCommandBuffer* cmd_buffers,
                          uint32_t cmd_buffer_count);

    // Convenience overload for a single command buffer.
    FrameResult end_frame(VulkanDevice& device,
                          VulkanSwapchain& swapchain,
                          uint32_t image_index,
                          VkCommandBuffer cmd_buffer) {
        return end_frame(device, swapchain, image_index, &cmd_buffer, 1);
    }

    uint32_t current_frame() const { return current_frame_; }
    static constexpr uint32_t frames_in_flight() { return kMaxFramesInFlight; }

private:
    VulkanDevice* device_ = nullptr;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    // Per-frame-in-flight acquire semaphore (current_frame_ keyed).
    // Signaled by vkAcquireNextImageKHR, consumed by vkQueueSubmit's wait.
    // Safe to reuse per frame slot because in_flight_fences_ guarantees the
    // previous submit (which consumed it) is complete.
    VkSemaphore image_available_[kMaxFramesInFlight]{};

    // Per-swapchain-image render-done semaphore (image_index keyed).
    // Signaled by vkQueueSubmit, waited on by vkQueuePresentKHR.
    // Must be per-image (not per-frame-in-flight) because without
    // VK_KHR_swapchain_maintenance1 the present operation holds the
    // semaphore until that image is re-acquired. Reusing it by frame slot
    // would signal a semaphore still in use by a previous present.
    std::vector<VkSemaphore> render_finished_;

    // Per-swapchain-image present fences (image_index keyed).
    std::vector<VkFence> present_fences_;

    // Per-frame-in-flight fences (current_frame_ keyed).
    VkFence in_flight_fences_[kMaxFramesInFlight]{};

    uint32_t current_frame_ = 0;
};

}  // namespace snt::render_backend
