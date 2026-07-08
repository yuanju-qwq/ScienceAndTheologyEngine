// CommandContext: lightweight wrapper around VkCommandBuffer.
//
// Exposes only what render graph passes need to record draw work, without
// giving upper layers raw Vulkan handles. Lifetime is tied to a single
// recording session (begin_recording -> ... -> end_recording).
//
// P2.2: minimal API — begin/end recording + raw handle accessor for
//        passes that need to call vkCmdBind* directly (temporary; will be
//        replaced by higher-level helpers in later phases).
// P2.3 (option B): begin_rendering / end_rendering helpers wrap Vulkan 1.3
//        dynamic rendering (vkCmdBeginRendering / vkCmdEndRendering).
//        Passes declare color + depth attachments via the graph; the
//        context sets up VkRenderingInfo + calls begin. The pass callback
//        then records draw commands freely; end_rendering closes the scope.
//
// Layering: owned by render_backend because VkCommandBuffer is a Vulkan
// primitive. The renderer layer holds CommandContext by reference.

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace snt::render_backend {

class VulkanDevice;

class CommandContext {
public:
    CommandContext() = default;
    ~CommandContext();

    // Non-copyable (owns a Vulkan command buffer).
    CommandContext(const CommandContext&) = delete;
    CommandContext& operator=(const CommandContext&) = delete;

    // Movable: required so std::vector<CommandContext> can resize.
    // Transfer ownership of the command buffer + device/pool pointers.
    CommandContext(CommandContext&& other) noexcept
        : device_(other.device_),
          command_pool_(other.command_pool_),
          command_buffer_(other.command_buffer_),
          recording_(other.recording_),
          owns_buffer_(other.owns_buffer_) {
        other.device_ = nullptr;
        other.command_pool_ = VK_NULL_HANDLE;
        other.command_buffer_ = VK_NULL_HANDLE;
        other.recording_ = false;
        other.owns_buffer_ = false;
    }

    CommandContext& operator=(CommandContext&& other) noexcept {
        if (this != &other) {
            reset();
            device_ = other.device_;
            command_pool_ = other.command_pool_;
            command_buffer_ = other.command_buffer_;
            recording_ = other.recording_;
            owns_buffer_ = other.owns_buffer_;
            other.device_ = nullptr;
            other.command_pool_ = VK_NULL_HANDLE;
            other.command_buffer_ = VK_NULL_HANDLE;
            other.recording_ = false;
            other.owns_buffer_ = false;
        }
        return *this;
    }

    // Allocate a primary command buffer from `pool` and begin recording.
    // `pool` must be a valid VkCommandPool created on the graphics family.
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> begin_recording(VulkanDevice& device, VkCommandPool pool);

    // End recording. After this, handle() can be submitted.
    void end_recording();

    // P2.3 (option B): begin dynamic rendering.
    // `rendering_info` is a fully-built VkRenderingInfo (color + depth
    // attachment info, render area, layer count). The caller (RenderGraph)
    // is responsible for resolving image views + formats from its resource
    // table before calling this. Must be called after begin_recording +
    // after pre-pass barriers. The pass callback then records draw commands.
    void begin_rendering(const VkRenderingInfo& rendering_info);

    // End dynamic rendering. Must be called after begin_rendering before
    // end_recording.
    void end_rendering();

    // Reset to pre-recording state (frees the command buffer if owned).
    // Called by RenderGraph after a submit to recycle the context.
    void reset();

    VkCommandBuffer handle() const { return command_buffer_; }
    bool is_recording() const { return recording_; }

private:
    VulkanDevice* device_ = nullptr;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    bool recording_ = false;
    bool owns_buffer_ = false;  // true if we allocated (not borrowed) the CB
};

}  // namespace snt::render_backend
