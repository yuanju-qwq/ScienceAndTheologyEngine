// RenderGraph: orchestrates pass declaration, dependency ordering, and
// execution of render passes.
//
// P2.1: skeleton (declarations only).
// P2.2: add_pass stores passes; execute() runs callbacks in registration
//        order, each on its own CommandContext, submitted serially.
// P2.3: dependency-driven ordering + automatic resource barriers.
// P2.4: wire ForwardPass + PipelineCache + migrate main.cpp.
//
// Reference: KhronosGroup/Vulkan-Samples framework + Granite render_graph.
//
// Layering note: this class sits ABOVE render_backend. It holds a
// VulkanDevice* + owns a VkCommandPool created on the graphics family.

#pragma once

#include "renderer/render_graph_pass.h"
#include "renderer/render_graph_resource.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <vector>

#include "core/expected.h"  // Expected<void> for init / execute / execute_record_only

namespace snt::render_backend {
class VulkanDevice;
}

namespace snt::renderer {

class RenderGraph {
public:
    RenderGraph();
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // Bind a Vulkan device + create the internal command pool.
    // `frames_in_flight` controls the CommandContext pool size — the graph
    // keeps `frames_in_flight` slots, each with its own CommandContext per
    // pass. This avoids pending-state conflicts when the caller pipelines
    // frames on the GPU (VulkanFrame-style frames-in-flight).
    // Must be called once before execute().
    snt::core::Expected<void> init(snt::render_backend::VulkanDevice& device,
              uint32_t frames_in_flight = 2);

    // Tear down command pool + all per-pass state.
    void destroy();

    // Create a transient texture resource. Backed by TransientPool (VMA
    // transient allocation, lifetime = single frame). The graph allocates
    // the actual VkImage lazily on first use + releases it on reset().
    RenderResource create_texture(const TextureDesc& desc);

    // Create a transient buffer resource. Backed by TransientPool.
    RenderResource create_buffer(const BufferDesc& desc);

    // Import an external VkImage + VkImageView as a RenderResource. The
    // graph does NOT own the underlying Vulkan objects; the caller is
    // responsible for their lifetime. Used for swapchain images, depth
    // buffers, and other resources created outside the graph.
    // `current_layout` is the layout the image is in at the start of the
    // frame; the graph uses it to insert correct barriers.
    // `terminal_layout` (optional) is the layout the image must be in
    // after all passes finish. The graph inserts a final barrier after
    // the last pass if the current layout differs. Pass
    // VK_IMAGE_LAYOUT_UNDEFINED to skip the terminal transition (default
    // for transient resources).
    // `width` / `height` / `mip_levels` / `array_layers` fill the texture
    // descriptor so the graph can derive render area + correct barrier
    // subresource ranges. Swapchain images: pass extent + 1 mip + 1 layer.
    RenderResource import_texture(VkImage image, VkImageView view,
                                  VkFormat format,
                                  VkImageLayout current_layout,
                                  uint32_t width, uint32_t height,
                                  uint32_t mip_levels = 1,
                                  uint32_t array_layers = 1,
                                  VkImageLayout terminal_layout =
                                      VK_IMAGE_LAYOUT_UNDEFINED);

    // Register a pass with the graph. Returns a non-owning pointer the
    // caller uses to fill in attachments + execute callback.
    // The graph owns the pass; pointer is valid until reset() or destruction.
    RenderGraphPass* add_pass(const std::string& name);

    // Execute all registered passes in registration order.
    //
    // Mode A (submit=true): each pass records into its own CommandContext,
    //   then the graph submits to the graphics queue + waits for completion
    //   before the next pass starts. This is the simplest correct execution
    //   model; P2.3 will replace serial submit with dependency scheduling.
    //
    // Mode B (submit=false): passes only record; the caller takes the
    //   recorded command buffers and submits them itself. Used when the
    //   caller needs to insert swapchain acquire/present around the
    //   recording (VulkanFrame-style integration).
    //
    // `frame_index` selects which frame slot's CommandContexts to use
    // (0..frames_in_flight-1). This must match the caller's frame-in-flight
    // slot so the recorded cb is not reset while still pending on the GPU.
    //
    // Returns an Error on any Vulkan failure.
    snt::core::Expected<void> execute();          // Mode A: record + submit (serial)
    snt::core::Expected<void> execute_record_only(uint32_t frame_index);  // Mode B: record only

    // Access the recorded command buffer for pass `pass_index` from the
    // `frame_index` slot. Only valid after a successful
    // execute_record_only(frame_index) call and before the next
    // begin_recording on that context.
    // Returns VK_NULL_HANDLE if indices are out of range.
    VkCommandBuffer recorded_command_buffer(uint32_t frame_index,
                                            size_t pass_index) const;

    // Collect all recorded command buffers from the `frame_index` slot into
    // `out_buffers`. Returns the count. Used when the caller wants to submit
    // all passes in one vkQueueSubmit (e.g. VulkanFrame::end_frame).
    uint32_t recorded_command_buffers(uint32_t frame_index,
                                      std::vector<VkCommandBuffer>* out_buffers) const;

    // Clear all passes + transient resources. Called between frames.
    // Does NOT destroy the command pool (kept across frames).
    void reset();

private:
    // PImpl to keep Vulkan deps out of the header.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::renderer
