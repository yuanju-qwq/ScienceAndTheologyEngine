// RenderSystem: ECS system that drives rendering from entity data.
//
// P2.D scope:
//   - Iterates View<Transform, MeshRef> to find the mesh entity.
//   - Reads active Camera entity to build view/proj.
//   - Registers a "forward" pass with RenderGraph; the pass callback
//     issues the actual vkCmd* calls (bind pipeline, bind descriptor set,
//     draw mesh). Dynamic rendering scope (vkCmdBeginRendering /
//     vkCmdEndRendering) is managed by RenderGraph based on the pass's
//     declared color/depth attachments.
//   - Calls RenderGraph::execute_record_only() to record into a
//     CommandContext, then hands the recorded command buffer to
//     VulkanFrame::end_frame() for submit + present.
//
// P2.3 (option B): swapchain + depth images are imported into the
// RenderGraph as external textures each frame. The forward pass declares
// them as color + depth attachments; the graph inserts layout barriers
// (UNDEFINED → COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR) + wraps the
// callback in vkCmdBeginRendering / vkCmdEndRendering.
//
// Layering: RenderSystem owns no Vulkan sync primitives. VulkanFrame owns
// fences/semaphores/acquire/present; RenderGraph owns command buffers +
// recording + barriers; RenderSystem owns the "what to draw" decision
// (ECS query) + attachment wiring.

#pragma once

#include "ecs/system.h"
#include "ecs/entt_config.h"
#include "renderer/render_graph.h"
#include "render_backend/vulkan_descriptor.h"

#include <functional>

namespace snt::render_backend {
class VulkanDevice;
class VulkanSwapchain;
class VulkanDepth;
class VulkanPipeline;
class VulkanDescriptor;
class VulkanFrame;
}

namespace snt::render {

class RenderSystem : public snt::ecs::System {
public:
    RenderSystem() = default;
    ~RenderSystem() override = default;

    // Wire up rendering dependencies. All pointers must outlive RenderSystem.
    void set_device(snt::render_backend::VulkanDevice* p)         { device_ = p; }
    void set_swapchain(snt::render_backend::VulkanSwapchain* p)   { swapchain_ = p; }
    void set_depth(snt::render_backend::VulkanDepth* p)           { depth_ = p; }
    void set_pipeline(snt::render_backend::VulkanPipeline* p)     { pipeline_ = p; }
    void set_descriptor(snt::render_backend::VulkanDescriptor* p) { descriptor_ = p; }
    void set_frame(snt::render_backend::VulkanFrame* p)           { frame_ = p; }

    // Set the entity to use as the active camera.
    void set_active_camera(entt::entity e) { active_camera_ = e; }

    // Optional: register a callback invoked inside the forward pass AFTER
    // mesh entity draws, within the same render pass scope (command buffer
    // is recording). Used by ChunkRenderSystem to record chunk draws into
    // the same command buffer without RenderSystem taking a hard dependency
    // on the voxel module. The callback receives:
    //   - cmd: the active command buffer
    //   - frame_idx: current frame-in-flight slot (for dynamic UBO writes)
    //   - view: per-frame view matrix (column-major 4x4 float array)
    //   - proj: per-frame projection matrix (column-major 4x4 float array)
    // The callback must capture any state it needs (e.g. World&, the
    // ChunkRenderSystem pointer) at registration time.
    using ForwardPassCallback =
        std::function<void(VkCommandBuffer, uint32_t,
                           const float[16], const float[16])>;
    void set_forward_pass_callback(ForwardPassCallback cb) {
        forward_pass_callback_ = std::move(cb);
    }

    // Optional: register a callback invoked at the END of the forward
    // pass, after mesh + chunk draws, within the same render pass scope.
    // Used by MuiRenderer to record UI draws (debug overlay text) on top
    // of the 3D scene. The callback receives the active command buffer +
    // the current frame-in-flight slot.
    using UiPassCallback =
        std::function<void(VkCommandBuffer, uint32_t)>;
    void set_ui_pass_callback(UiPassCallback cb) {
        ui_pass_callback_ = std::move(cb);
    }

    // Initialize the RenderGraph (creates its command pool). Must be called
    // after set_device() and before update(). Returns an Error on failure.
    snt::core::Expected<void> init_render_graph();

    // Release RenderGraph resources.
    void destroy_render_graph();

    // ECS update: build MVP, register forward pass, record + submit.
    void update(snt::ecs::World& world, float dt) override;

    // Returns true if the last frame signaled swapchain-out-of-date.
    bool needs_resize() const { return needs_resize_; }

private:
    snt::render_backend::VulkanDevice*     device_      = nullptr;
    snt::render_backend::VulkanSwapchain*  swapchain_   = nullptr;
    snt::render_backend::VulkanDepth*      depth_       = nullptr;
    snt::render_backend::VulkanPipeline*   pipeline_    = nullptr;
    snt::render_backend::VulkanDescriptor* descriptor_  = nullptr;
    snt::render_backend::VulkanFrame*      frame_       = nullptr;
    entt::entity active_camera_ = entt::null;

    snt::renderer::RenderGraph graph_;
    bool graph_initialized_ = false;

    bool needs_resize_ = false;

    // Optional extra draw callbacks (chunk rendering + UI overlay).
    ForwardPassCallback forward_pass_callback_;
    UiPassCallback      ui_pass_callback_;
};

}  // namespace snt::render
