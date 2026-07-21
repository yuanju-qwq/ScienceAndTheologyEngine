// RenderSystem implementation.
//
// P2.D + P2.3 (option B): ECS-driven rendering via RenderGraph with
// dynamic rendering.
// Per-frame flow:
//   1. Query ECS for active Camera (Transform + Camera) + first mesh entity
//      (Transform + MeshRef). Build MVP matrix.
//   2. Update the per-frame UBO via VulkanDescriptor.
//   3. VulkanFrame::begin_frame() — wait fence + acquire swapchain image.
//   4. Import swapchain image + depth image into RenderGraph as external
//      textures (swapchain terminal layout = PRESENT_SRC_KHR).
//   5. Register a "forward" pass with RenderGraph. The pass declares the
//      swapchain image as color attachment + depth image as depth
//      attachment. The graph inserts layout barriers + wraps the callback
//      in vkCmdBeginRendering / vkCmdEndRendering. The callback:
//        vkCmdBindPipeline / vkCmdSetViewport / vkCmdSetScissor /
//        vkCmdBindDescriptorSets / mesh.draw()
//   6. RenderGraph::execute_record_only() — records the pass callback into
//      its CommandContext.
//   7. VulkanFrame::end_frame(image_index, recorded_cb) — submit + present.

#define SNT_LOG_CHANNEL "render"
#include "core/log.h"

#include "render/render_system.h"

#include "assets/asset_manager.h"
#include "core/profiling.h"
#include "render/render_components.h"
#include "ecs/world.h"
#include "render_backend/command_context.h"
#include "render_backend/vulkan_depth.h"
#include "render_backend/vulkan_descriptor.h"
#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_frame.h"
#include "render_backend/vulkan_mesh.h"
#include "render_backend/vulkan_pipeline.h"
#include "render_backend/vulkan_swapchain.h"

#include <volk.h>

// GLM templates call the standard `assert` macro; pull it in explicitly
// so the macro is visible regardless of include order.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <vector>

namespace snt::render {

namespace {

// Build a model matrix from a Transform component.
glm::mat4 build_model_matrix(const snt::render::Transform& t) {
    using namespace glm;
    mat4 m = translate(mat4(1.0f), vec3(t.position[0], t.position[1], t.position[2]));
    m = rotate(m, radians(t.rotation[2]), vec3(0, 0, 1));  // roll
    m = rotate(m, radians(t.rotation[0]), vec3(1, 0, 0));  // pitch
    m = rotate(m, radians(t.rotation[1]), vec3(0, 1, 0));  // yaw
    m = scale(m, vec3(t.scale[0], t.scale[1], t.scale[2]));
    return m;
}

// Build a view matrix from a camera Transform (position + yaw/pitch).
glm::mat4 build_view_matrix(const snt::render::Transform& cam) {
    using namespace glm;
    float yaw_rad = radians(cam.rotation[1]);
    float pitch_rad = radians(cam.rotation[0]);
    vec3 pos(cam.position[0], cam.position[1], cam.position[2]);
    vec3 forward(
        cos(pitch_rad) * cos(yaw_rad),
        sin(pitch_rad),
        cos(pitch_rad) * sin(yaw_rad));
    return lookAt(pos, pos + forward, vec3(0, 1, 0));
}

float sanitize_color_component(float value, float fallback) noexcept {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, 0.0f, 16.0f);
}

void sanitize_light(snt::render::DirectionalLight& light,
                    const snt::render::DirectionalLight& fallback) noexcept {
    const float length_squared = light.direction_to_light[0] * light.direction_to_light[0] +
        light.direction_to_light[1] * light.direction_to_light[1] +
        light.direction_to_light[2] * light.direction_to_light[2];
    if (!std::isfinite(length_squared) || length_squared < 0.000001f) {
        light.direction_to_light = fallback.direction_to_light;
    }
    for (size_t index = 0; index < light.color.size(); ++index) {
        light.color[index] = sanitize_color_component(light.color[index], fallback.color[index]);
    }
    light.intensity = sanitize_color_component(light.intensity, fallback.intensity);
}

snt::render::EnvironmentLighting sanitize_lighting(
    snt::render::EnvironmentLighting lighting) noexcept {
    const snt::render::EnvironmentLighting defaults{};
    sanitize_light(lighting.sun, defaults.sun);
    sanitize_light(lighting.moon, defaults.moon);
    for (size_t index = 0; index < lighting.ambient_color.size(); ++index) {
        lighting.ambient_color[index] = sanitize_color_component(
            lighting.ambient_color[index], defaults.ambient_color[index]);
    }
    lighting.ambient_intensity = sanitize_color_component(
        lighting.ambient_intensity, defaults.ambient_intensity);
    for (size_t index = 0; index < lighting.sky_color.size(); ++index) {
        lighting.sky_color[index] = sanitize_color_component(
            lighting.sky_color[index], defaults.sky_color[index]);
    }
    return lighting;
}

void apply_environment_lighting(snt::render_backend::UniformBufferObject& ubo,
                                const snt::render::EnvironmentLighting& lighting) noexcept {
    std::copy(lighting.sun.direction_to_light.begin(), lighting.sun.direction_to_light.end(),
              ubo.sun_direction_intensity);
    ubo.sun_direction_intensity[3] = lighting.sun.intensity;
    std::copy(lighting.sun.color.begin(), lighting.sun.color.end(), ubo.sun_color);
    std::copy(lighting.moon.direction_to_light.begin(), lighting.moon.direction_to_light.end(),
              ubo.moon_direction_intensity);
    ubo.moon_direction_intensity[3] = lighting.moon.intensity;
    std::copy(lighting.moon.color.begin(), lighting.moon.color.end(), ubo.moon_color);
    std::copy(lighting.ambient_color.begin(), lighting.ambient_color.end(),
              ubo.ambient_color_intensity);
    ubo.ambient_color_intensity[3] = lighting.ambient_intensity;
}

}  // namespace

void RenderSystem::set_environment_lighting(EnvironmentLighting lighting) noexcept {
    lighting_ = sanitize_lighting(std::move(lighting));
}

snt::core::Expected<void> RenderSystem::init_render_graph() {
    if (!device_ || !frame_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "RenderSystem::init_render_graph: device/frame not set"};
    }
    if (graph_initialized_) return {};
    // Match VulkanFrame's frames-in-flight so each frame slot has its own
    // command buffer, avoiding pending-state conflicts across frames.
    if (auto r = graph_.init(*device_, snt::render_backend::VulkanFrame::frames_in_flight());
        !r) {
        snt::core::Error e = r.error();
        e.with_context("RenderSystem::init_render_graph");
        return e;
    }
    graph_initialized_ = true;
    return {};
}

void RenderSystem::destroy_render_graph() {
    if (graph_initialized_) {
        graph_.destroy();
        graph_initialized_ = false;
    }
    // GPU mesh residency is owned by AssetManager and released through its
    // uploader lifecycle during Runtime::shutdown().
}

void RenderSystem::update(snt::ecs::World& world, float /*dt*/) {
    SNT_PROFILE_FUNCTION();  // Profiling zone (no-op until a backend is wired in)
    if (!device_ || !swapchain_ || !depth_ || !pipeline_ ||
        !descriptor_ || !frame_ || !assets_ || !graph_initialized_) {
        return;
    }
    if (active_camera_ == entt::null) return;

    auto& registry = world.registry();
    if (!registry.all_of<snt::render::Transform, snt::render::Camera>(active_camera_)) {
        return;
    }

    auto& cam_transform = registry.get<snt::render::Transform>(active_camera_);
    auto& cam_comp      = registry.get<snt::render::Camera>(active_camera_);

    // Build view + projection from camera.
    glm::mat4 view = build_view_matrix(cam_transform);
    glm::mat4 proj = glm::perspective(glm::radians(cam_comp.fov),
                                      cam_comp.aspect,
                                      cam_comp.near_plane,
                                      cam_comp.far_plane);
    // GLM defaults to OpenGL clip space (Y up); Vulkan uses Y down.
    proj[1][1] *= -1.0f;

    // --- Collect mesh entities (single ECS pass) ---
    // P2.4: iterate ALL entities with Transform + MeshRef. For each:
    //   - select optional generic MeshLod from camera distance
    //   - resolve mesh handle via AssetManager
    //   - precompute model matrix (view/proj are per-frame, same for all)
    //   - stash a MeshDraw entry; UBO write happens AFTER begin_frame
    //     (which fence-waits + selects the frame-in-flight slot).
    struct MeshDraw {
        snt::render_backend::VulkanMesh* mesh;
        uint32_t ubo_offset;            // dynamic offset for this entity's MVP slot
        snt::render_backend::UniformBufferObject ubo;  // precomputed MVP
    };
    std::vector<MeshDraw> draws;
    draws.reserve(32);

    uint32_t entity_index = 0;
    auto view_group = registry.view<snt::render::Transform, snt::render::MeshRef>();
    const uint32_t max_entities = descriptor_ ? descriptor_->max_entities() : 0;
    for (auto e : view_group) {
        if (entity_index >= max_entities) {
            SNT_LOG_ERROR("too many mesh entities, truncating");
            break;
        }

        auto& transform = registry.get<snt::render::Transform>(e);
        auto& mesh_ref  = registry.get<snt::render::MeshRef>(e);

        const float dx = transform.position[0] - cam_transform.position[0];
        const float dy = transform.position[1] - cam_transform.position[1];
        const float dz = transform.position[2] - cam_transform.position[2];
        const MeshLod* lod = registry.try_get<snt::render::MeshLod>(e);
        const MeshLodSelection selection = select_mesh_lod(
            mesh_ref, lod, dx * dx + dy * dy + dz * dz);
        if (selection.level == MeshLodLevel::kCulled) continue;

        // Resolve the mesh handle to a VulkanMesh via the AssetManager.
        auto* mesh = assets_->mesh(selection.handle);
        if (!mesh) {
            SNT_LOG_ERROR("entity %u: invalid mesh handle",
                          static_cast<unsigned>(e));
            continue;
        }

        // Precompute MVP. view/proj are constant for this frame; only
        // model varies per entity.
        glm::mat4 model = build_model_matrix(transform);
        snt::render_backend::UniformBufferObject ubo{};
        std::memcpy(ubo.model, glm::value_ptr(model), sizeof(ubo.model));
        std::memcpy(ubo.view,  glm::value_ptr(view),  sizeof(ubo.view));
        std::memcpy(ubo.proj,  glm::value_ptr(proj),  sizeof(ubo.proj));
        apply_environment_lighting(ubo, lighting_);

        draws.push_back({mesh, entity_index * descriptor_->ubo_stride(), ubo});
        ++entity_index;
    }

    // Only skip the frame when no built-in mesh draw and no external pass
    // providers exist. Providers (voxel/UI/etc.) may still need the frame
    // even when the ECS mesh view is empty.
    if (draws.empty() && pass_providers_.empty()) return;

    // --- Acquire swapchain image ---
    uint32_t image_index = 0;
    auto acquire_result = frame_->begin_frame(*device_, *swapchain_, &image_index);
    if (acquire_result == snt::render_backend::VulkanFrame::FrameResult::kResized) {
        needs_resize_ = true;
        return;
    }
    if (acquire_result == snt::render_backend::VulkanFrame::FrameResult::kError) {
        SNT_LOG_ERROR("begin_frame failed");
        return;
    }

    // Now that we know the frame-in-flight slot, write each entity's MVP
    // into its slot in the dynamic UBO.
    uint32_t frame_idx = frame_->current_frame();
    for (uint32_t i = 0; i < draws.size(); ++i) {
        descriptor_->update_ubo(frame_idx, i, draws[i].ubo);
    }

    // --- P2.3 (option B): import swapchain + depth into RenderGraph ---
    // The swapchain image must end the frame in PRESENT_SRC_KHR; declare
    // it as terminal_layout so the graph inserts a final barrier.
    graph_.reset();

    const VkExtent2D extent = swapchain_->extent();
    const VkFormat color_format = swapchain_->image_format();
    const VkFormat depth_format = depth_->format();

    // Swapchain image for THIS frame's image_index. Initial layout is
    // UNDEFINED (acquired fresh); terminal layout is PRESENT_SRC_KHR.
    VkImage swapchain_image = swapchain_->images()[image_index];
    VkImageView swapchain_view = swapchain_->image_views()[image_index];
    auto color_res = graph_.import_texture(
        swapchain_image, swapchain_view, color_format,
        VK_IMAGE_LAYOUT_UNDEFINED,
        extent.width, extent.height,
        /*mip_levels=*/1, /*array_layers=*/1,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // Depth image. Initial layout UNDEFINED (cleared each frame); no
    // terminal requirement (stays at DEPTH_ATTACHMENT_OPTIMAL).
    VkImage depth_image = depth_->image();
    VkImageView depth_view = depth_->view();
    auto depth_res = graph_.import_texture(
        depth_image, depth_view, depth_format,
        VK_IMAGE_LAYOUT_UNDEFINED,
        extent.width, extent.height);

    // --- Register the built-in mesh forward pass ---
    auto* pipeline  = pipeline_;
    auto* descriptor = descriptor_;

    auto* pass = graph_.add_pass("mesh_forward");
    if (!pass) {
        SNT_LOG_ERROR("add_pass failed");
        return;
    }

    // Declare color + depth attachments. The graph will transition them to
    // COLOR_ATTACHMENT_OPTIMAL / DEPTH_STENCIL_ATTACHMENT_OPTIMAL before
    // the callback + wrap the callback in vkCmdBeginRendering / EndRendering.
    snt::renderer::ColorAttachmentDecl color_decl{
        .resource = color_res,
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
        .clear_value = {.color = {{lighting_.sky_color[0], lighting_.sky_color[1],
                                   lighting_.sky_color[2], lighting_.sky_color[3]}}},
    };
    pass->color_attachments.push_back(color_decl);

    snt::renderer::DepthAttachmentDecl depth_decl{
        .resource = depth_res,
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
        .clear_value = {.depthStencil = {1.0f, 0}},
    };
    pass->depth_attachment = depth_decl;

    // Capture draws by value (vector copy) — the callback runs synchronously
    // inside execute_record_only, so this is safe. The callback does NOT
    // call vkCmdBeginRenderPass / EndRenderPass; the graph handles dynamic
    // rendering scope based on the declared attachments.
    pass->execute = [pipeline, descriptor, frame_idx, extent, draws]
                    (snt::render_backend::CommandContext& ctx) {
        VkCommandBuffer cmd = ctx.handle();

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle());

        VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{
            .offset = {0, 0},
            .extent = extent,
        };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Draw each mesh entity with its own dynamic UBO offset.
        VkDescriptorSet desc_set = descriptor->descriptor_set(frame_idx);
        for (const auto& d : draws) {
            uint32_t dyn_offset = d.ubo_offset;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline->layout(), 0, 1, &desc_set,
                                    1, &dyn_offset);
            d.mesh->draw(cmd);
        }
    };

    // Let feature modules append their own graph passes. Ordering over the
    // main color/depth target is explicit: providers depend on the current
    // `last_color_pass` and update it when they add a pass.
    RenderPassBuildContext build_ctx{
        graph_, color_res, depth_res, extent, frame_idx, lighting_
    };
    build_ctx.last_color_pass = "mesh_forward";
    std::memcpy(build_ctx.view.data(), glm::value_ptr(view), sizeof(float) * 16);
    std::memcpy(build_ctx.proj.data(), glm::value_ptr(proj), sizeof(float) * 16);
    for (auto& provider : pass_providers_) {
        if (provider) {
            provider(build_ctx);
        }
    }

    // --- Record (no submit) ---
    // Use the same frame_index as VulkanFrame's current_frame() so the
    // recorded command buffer belongs to the same frame slot whose fence
    // we will wait on next time around. This prevents resetting a command
    // buffer that is still pending on the GPU.
    uint32_t frame_index = frame_->current_frame();
    if (auto r = graph_.execute_record_only(frame_index); !r) {
        SNT_LOG_ERROR("execute_record_only failed: %s",
                      r.error().with_context("RenderSystem::update").format().c_str());
        return;
    }

    // Collect all recorded command buffers (one per pass) + submit them
    // together in one vkQueueSubmit. P2.3.4's topological sort guarantees
    // they are in dependency order.
    std::vector<VkCommandBuffer> recorded_cbs;
    uint32_t cb_count = graph_.recorded_command_buffers(frame_index, &recorded_cbs);
    if (cb_count == 0) {
        SNT_LOG_ERROR("no recorded command buffers");
        return;
    }

    // --- Submit + present ---
    auto end_result = frame_->end_frame(*device_, *swapchain_, image_index,
                                        recorded_cbs.data(), cb_count);
    if (end_result == snt::render_backend::VulkanFrame::FrameResult::kResized) {
        needs_resize_ = true;
    } else if (end_result == snt::render_backend::VulkanFrame::FrameResult::kError) {
        SNT_LOG_ERROR("end_frame failed");
    } else {
        needs_resize_ = false;
    }
}

}  // namespace snt::render
