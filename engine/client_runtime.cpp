// ClientRuntime implementation.

#define SNT_LOG_CHANNEL "client_runtime"
#include "engine/client_runtime.h"

#include "engine/client_services.h"
#include "engine/client_session.h"

#include "assets/asset_manager.h"
#include "core/events.h"
#include "core/log.h"
#include "render/render_components.h"
#include "ecs/event_bus.h"
#include "ecs/world.h"
#include "input/input_system.h"
#include "platform/window.h"
#include "render/render_system.h"
#include "render_backend/command_context.h"
#include "render_backend/vertex_buffer_pool.h"
#include "render_backend/vulkan_depth.h"
#include "render_backend/vulkan_descriptor.h"
#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_frame.h"
#include "render_backend/vulkan_instance.h"
#include "render_backend/vulkan_mesh.h"
#include "render_backend/vulkan_pipeline.h"
#include "render_backend/vulkan_swapchain.h"
#include "ui/mui_renderer.h"
#include "ui/retained_mui.h"
#include "voxel/chunk_renderer.h"
#include "voxel/chunk_render_system.h"

#include <SDL3/SDL.h>
#include <volk.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace snt::engine {
namespace {

struct FpsTracker {
    static constexpr int kSamples = 120;

    float frame_times_ms[kSamples] = {};
    int offset = 0;
    float last_frame_ms = 0.0f;

    void tick(float milliseconds) {
        frame_times_ms[offset] = milliseconds;
        offset = (offset + 1) % kSamples;
        last_frame_ms = milliseconds;
    }

    float fps() const {
        float sum = 0.0f;
        constexpr int kAverageSamples = 60;
        for (int index = 0; index < kAverageSamples; ++index) {
            const int sample = (offset - 1 - index + kSamples) % kSamples;
            sum += frame_times_ms[sample];
        }
        const float average_ms = sum / static_cast<float>(kAverageSamples);
        return average_ms > 0.0f ? 1000.0f / average_ms : 0.0f;
    }
};

bool same_ui_clip(const snt::ui::UiClipRect& left,
                  const snt::ui::UiClipRect& right) {
    return left.enabled == right.enabled &&
           (!left.enabled ||
            (left.rect.pos.x == right.rect.pos.x &&
             left.rect.pos.y == right.rect.pos.y &&
             left.rect.size.x == right.rect.size.x &&
             left.rect.size.y == right.rect.size.y));
}

void append_draw_data(snt::ui::UiDrawData& destination,
                      const snt::ui::UiDrawData& source) {
    if (source.vertices.empty() || source.indices.empty()) return;
    if (destination.vertices.size() > std::numeric_limits<snt::ui::UiIndex>::max() -
                                      source.vertices.size()) {
        SNT_LOG_WARN("UI draw data overflow while appending; dropping appended batch");
        return;
    }

    if (source.glyph_atlas) {
        if (!destination.glyph_atlas) {
            destination.glyph_atlas = source.glyph_atlas;
        } else if (destination.glyph_atlas.get() != source.glyph_atlas.get()) {
            SNT_LOG_ERROR("MUI draw batches reference different glyph atlases; batch rejected");
            return;
        }
    }
    if (source.image_atlas) {
        if (!destination.image_atlas) {
            destination.image_atlas = source.image_atlas;
        } else if (destination.image_atlas.get() != source.image_atlas.get()) {
            SNT_LOG_ERROR("MUI draw batches reference different image atlases; batch rejected");
            return;
        }
    }

    const auto base = static_cast<snt::ui::UiIndex>(destination.vertices.size());
    const uint32_t first_index = static_cast<uint32_t>(destination.indices.size());
    destination.vertices.insert(destination.vertices.end(), source.vertices.begin(), source.vertices.end());
    destination.indices.reserve(destination.indices.size() + source.indices.size());
    for (const snt::ui::UiIndex index : source.indices) {
        destination.indices.push_back(base + index);
    }

    const auto append_batch = [&destination, first_index](snt::ui::UiDrawBatch batch) {
        batch.first_index += first_index;
        if (!destination.batches.empty()) {
            auto& last = destination.batches.back();
            if (last.texture == batch.texture && same_ui_clip(last.clip, batch.clip) &&
                last.first_index + last.index_count == batch.first_index) {
                last.index_count += batch.index_count;
                return;
            }
        }
        destination.batches.push_back(batch);
    };
    if (source.batches.empty()) {
        append_batch({
            .first_index = 0,
            .index_count = static_cast<uint32_t>(source.indices.size()),
            .texture = snt::ui::UiTextureBinding::GlyphAtlas,
        });
    } else {
        for (const snt::ui::UiDrawBatch& batch : source.batches) {
            if (batch.first_index > source.indices.size() ||
                batch.index_count > source.indices.size() - batch.first_index) {
                SNT_LOG_ERROR("UI draw data contains an invalid batch range; batch rejected");
                continue;
            }
            append_batch(batch);
        }
    }
}

snt::ui::UiInputState make_ui_input_state(const snt::input::InputState& input,
                                          const snt::ui::UiViewport& viewport,
                                          bool ui_input_enabled) {
    snt::ui::UiInputState result;
    result.pointer_enabled = ui_input_enabled;
    const snt::ui::Vec2 window_pointer{
        static_cast<float>(input.mouse_x),
        static_cast<float>(input.mouse_y),
    };
    result.pointer_position = viewport.valid()
        ? viewport.window_to_logical(window_pointer)
        : window_pointer;
    result.scroll_delta = {input.mouse_wheel_x, input.mouse_wheel_y};
    for (size_t index = 0; index < result.pointer_held.size(); ++index) {
        result.pointer_held[index] = input.mouse_held[index];
        result.pointer_pressed[index] = input.mouse_pressed[index];
        result.pointer_released[index] = input.mouse_released[index];
    }

    if (!ui_input_enabled) return result;
    const auto add_key = [&result, &input](int scancode, snt::ui::UiKey key) {
        if (scancode >= 0 && scancode < static_cast<int>(snt::input::kKeyCount) &&
            input.key_pressed[scancode]) {
            result.pressed_keys.push_back(key);
        }
    };
    add_key(SDL_SCANCODE_RETURN, snt::ui::UiKey::Enter);
    add_key(SDL_SCANCODE_SPACE, snt::ui::UiKey::Space);
    add_key(SDL_SCANCODE_ESCAPE, snt::ui::UiKey::Escape);
    add_key(SDL_SCANCODE_TAB, snt::ui::UiKey::Tab);
    add_key(SDL_SCANCODE_BACKSPACE, snt::ui::UiKey::Backspace);
    add_key(SDL_SCANCODE_DELETE, snt::ui::UiKey::Delete);
    add_key(SDL_SCANCODE_HOME, snt::ui::UiKey::Home);
    add_key(SDL_SCANCODE_END, snt::ui::UiKey::End);
    add_key(SDL_SCANCODE_LEFT, snt::ui::UiKey::Left);
    add_key(SDL_SCANCODE_RIGHT, snt::ui::UiKey::Right);
    add_key(SDL_SCANCODE_UP, snt::ui::UiKey::Up);
    add_key(SDL_SCANCODE_DOWN, snt::ui::UiKey::Down);
    result.text_commits = input.text_commits;
    result.text_compositions.reserve(input.text_compositions.size());
    for (const snt::input::TextComposition& composition : input.text_compositions) {
        result.text_compositions.push_back({
            .text = composition.text,
            .start = composition.start,
            .length = composition.length,
        });
    }
    return result;
}

}  // namespace

struct ClientRuntime::Impl {
    snt::platform::Window window;
    snt::input::InputSystem input_system;

    snt::render_backend::VulkanInstance vk_instance;
    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    snt::render_backend::VulkanDevice vk_device;
    snt::render_backend::VulkanSwapchain vk_swapchain;
    snt::render_backend::VulkanDepth vk_depth;
    snt::render_backend::VulkanDescriptor vk_descriptor;
    snt::render_backend::VulkanPipeline vk_pipeline;
    snt::render_backend::VulkanFrame vk_frame;
    snt::assets::AssetManager asset_manager;

    snt::render::RenderSystem render_system;
    entt::entity active_camera = entt::null;
    std::unique_ptr<snt::voxel::ChunkRenderer> chunk_renderer;
    std::shared_ptr<snt::voxel::ChunkRenderSystem> runtime_chunk_render_system;

    std::unique_ptr<snt::ui::MuiRenderer> mui_renderer;
    std::unique_ptr<snt::ui::UiRuntime> ui_runtime;
    snt::ui::UiDrawData ui_draw_data;

    FpsTracker fps_tracker;
    ClientRuntimeStats stats;
    float ui_user_scale = 1.0f;
    bool mouse_locked = false;
    IClientSession* session = nullptr;
    std::unique_ptr<ClientWorldSession> world_session;
};

ClientRuntime::ClientRuntime() : impl_(std::make_unique<Impl>()) {}
ClientRuntime::~ClientRuntime() { shutdown(); }

snt::core::Expected<void> ClientRuntime::init(
    const snt::core::RuntimeConfig& config,
    snt::core::RuntimePaths runtime_paths,
    std::unique_ptr<IClientSession> session) {
    using namespace snt::platform;
    using namespace snt::render_backend;

    if (!session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "ClientRuntime::init requires an IClientSession"};
    }
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ClientRuntime::init called twice"};
    }

    if (auto result = simulation_.init_services(config, std::move(runtime_paths)); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(simulation services)");
        return error;
    }
    const auto& paths = simulation_.services().paths();

    if (auto result = impl_->window.create(WindowDesc{
            .title = config.window.title,
            .width = config.window.width,
            .height = config.window.height,
            .resizable = config.window.resizable,
            .vulkan_enabled = config.window.vulkan_enabled,
        }); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(window)");
        return error;
    }
    const auto window_metrics = impl_->window.metrics();
    SNT_LOG_INFO("Client window created: window=%dx%d framebuffer=%dx%d dpi=%.2f",
                 window_metrics.window_size.width, window_metrics.window_size.height,
                 window_metrics.pixel_size.width, window_metrics.pixel_size.height,
                 window_metrics.display_scale);

    auto& simulation_events = simulation_.world_session().events();
    simulation_events.sink<snt::core::SdlEventFired>()
        .connect<&snt::input::InputSystem::on_sdl_event>(&impl_->input_system);
    impl_->window.set_event_callback([this](const void* sdl_event) {
        auto& events = simulation_.world_session().events();
        events.enqueue<snt::core::SdlEventFired>({sdl_event});
        events.update();
    });

    if (auto result = impl_->vk_instance.init(impl_->window); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(vk_instance)");
        return error;
    }
    uint64_t surface_bits = 0;
    if (auto result = impl_->window.create_vulkan_surface(
            reinterpret_cast<void*>(impl_->vk_instance.handle()), &surface_bits); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(vk_surface)");
        return error;
    }
    impl_->vk_surface = reinterpret_cast<VkSurfaceKHR>(surface_bits);

    if (auto result = impl_->vk_device.init(impl_->vk_instance.handle(), impl_->vk_surface); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(vk_device)");
        return error;
    }
    if (auto result = impl_->asset_manager.init(
            &impl_->vk_device, simulation_.services().content_source(),
            simulation_.services().asset_catalog()); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(AssetManager)");
        return error;
    }
    if (auto result = impl_->vk_swapchain.init(
            impl_->vk_device,
            static_cast<uint32_t>(window_metrics.pixel_size.width),
            static_cast<uint32_t>(window_metrics.pixel_size.height)); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(vk_swapchain)");
        return error;
    }
    if (auto result = impl_->vk_depth.init(impl_->vk_device, impl_->vk_swapchain); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(vk_depth)");
        return error;
    }
    if (auto result = impl_->vk_descriptor.init(impl_->vk_device, config.render.max_entities); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(vk_descriptor)");
        return error;
    }
    if (auto result = impl_->vk_pipeline.init(
            impl_->vk_device, impl_->vk_descriptor, impl_->vk_swapchain.image_format(),
            impl_->vk_depth.format(), paths.resolve_engine(config.render.vert_shader_path),
            paths.resolve_engine(config.render.frag_shader_path),
            VkVertexInputBindingDescription{
                .binding = 0,
                .stride = sizeof(snt::render_backend::MeshVertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            },
            std::vector<VkVertexInputAttributeDescription>{
                {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
                 .offset = offsetof(snt::render_backend::MeshVertex, position)},
                {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
                 .offset = offsetof(snt::render_backend::MeshVertex, color)},
            }); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(vk_pipeline)");
        return error;
    }
    if (auto result = impl_->vk_frame.init(
            impl_->vk_device, static_cast<uint32_t>(impl_->vk_swapchain.image_views().size())); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(vk_frame)");
        return error;
    }

    impl_->render_system.set_device(&impl_->vk_device);
    impl_->render_system.set_swapchain(&impl_->vk_swapchain);
    impl_->render_system.set_depth(&impl_->vk_depth);
    impl_->render_system.set_pipeline(&impl_->vk_pipeline);
    impl_->render_system.set_descriptor(&impl_->vk_descriptor);
    impl_->render_system.set_frame(&impl_->vk_frame);
    impl_->render_system.set_assets(&impl_->asset_manager);
    if (auto result = impl_->render_system.init_render_graph(); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(render_graph)");
        return error;
    }

    impl_->chunk_renderer = std::make_unique<snt::voxel::ChunkRenderer>();
    if (auto result = impl_->chunk_renderer->init(
            impl_->vk_device, paths, impl_->vk_swapchain.image_format(), impl_->vk_depth.format(),
            paths.resolve_engine("shaders/voxel.vert.spv"),
            paths.resolve_engine("shaders/voxel.frag.spv"), config.voxel.max_chunks); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(chunk_renderer)");
        return error;
    }
    auto chunk_system = std::make_shared<snt::voxel::ChunkRenderSystem>(simulation_.services().jobs());
    chunk_system->set_chunk_renderer(impl_->chunk_renderer.get());
    chunk_system->set_chunk_registry(&simulation_.world_session().chunks());
    chunk_system->set_remesh_jobs_per_frame(config.voxel.remesh_jobs_per_frame);
    chunk_system->set_uploads_per_frame(config.voxel.uploads_per_frame);
    if (auto result = simulation_.world_session().register_main_system(chunk_system); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(register ChunkRenderSystem)");
        return error;
    }
    impl_->runtime_chunk_render_system = std::move(chunk_system);
    impl_->render_system.add_pass_provider(
        [chunk_system_ptr = impl_->runtime_chunk_render_system.get()]
        (snt::render::RenderPassBuildContext& context) {
            auto* pass = context.graph.add_pass("voxel_chunks");
            if (!pass) {
                SNT_LOG_ERROR("Failed to add voxel_chunks render pass");
                return;
            }
            if (!context.last_color_pass.empty()) pass->depends_on.push_back(context.last_color_pass);
            pass->color_attachments.push_back(snt::renderer::ColorAttachmentDecl{
                .resource = context.color_resource,
                .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                .store_op = VK_ATTACHMENT_STORE_OP_STORE,
            });
            pass->depth_attachment = snt::renderer::DepthAttachmentDecl{
                .resource = context.depth_resource,
                .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                .store_op = VK_ATTACHMENT_STORE_OP_STORE,
            };

            const auto view = context.view;
            const auto projection = context.proj;
            const auto extent = context.extent;
            const uint32_t frame_index = context.frame_idx;
            pass->execute = [chunk_system_ptr, view, projection, extent, frame_index]
                            (snt::render_backend::CommandContext& pass_context) {
                VkCommandBuffer command_buffer = pass_context.handle();
                VkViewport viewport{.x = 0.0f, .y = 0.0f,
                                    .width = static_cast<float>(extent.width),
                                    .height = static_cast<float>(extent.height),
                                    .minDepth = 0.0f, .maxDepth = 1.0f};
                vkCmdSetViewport(command_buffer, 0, 1, &viewport);
                VkRect2D scissor{.offset = {0, 0}, .extent = extent};
                vkCmdSetScissor(command_buffer, 0, 1, &scissor);
                chunk_system_ptr->render(command_buffer, frame_index, view.data(), projection.data());
            };
            context.last_color_pass = pass->name;
        });

    snt::ui::TextEngineConfig text_config;
    text_config.font_paths = config.ui.font_paths;
    text_config.locale = config.ui.locale;
    text_config.icu_data_path = config.ui.icu_data_path;
    impl_->ui_runtime = std::make_unique<snt::ui::UiRuntime>(paths, std::move(text_config));
    impl_->ui_user_scale = config.ui.scale;
    if (!impl_->ui_runtime->text_available()) {
        SNT_LOG_ERROR("MUI text initialization failed: %s",
                      impl_->ui_runtime->text_initialization_error().c_str());
    }

    impl_->mui_renderer = std::make_unique<snt::ui::MuiRenderer>();
    if (auto result = impl_->mui_renderer->init(
            impl_->vk_device, impl_->vk_swapchain.image_format(), paths); !result) {
        SNT_LOG_ERROR("MuiRenderer init failed: %s", result.error().format().c_str());
        impl_->mui_renderer.reset();
    } else {
        auto* renderer = impl_->mui_renderer.get();
        auto* draw_data = &impl_->ui_draw_data;
        impl_->render_system.add_pass_provider(
            [renderer, draw_data](snt::render::RenderPassBuildContext& context) {
                auto* pass = context.graph.add_pass("ui_overlay");
                if (!pass) {
                    SNT_LOG_ERROR("Failed to add ui_overlay render pass");
                    return;
                }
                if (!context.last_color_pass.empty()) pass->depends_on.push_back(context.last_color_pass);
                pass->color_attachments.push_back(snt::renderer::ColorAttachmentDecl{
                    .resource = context.color_resource,
                    .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                });
                const auto extent = context.extent;
                pass->execute = [renderer, draw_data, extent]
                                (snt::render_backend::CommandContext& pass_context) {
                    VkCommandBuffer command_buffer = pass_context.handle();
                    VkViewport viewport{.x = 0.0f, .y = 0.0f,
                                        .width = static_cast<float>(extent.width),
                                        .height = static_cast<float>(extent.height),
                                        .minDepth = 0.0f, .maxDepth = 1.0f};
                    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
                    VkRect2D scissor{.offset = {0, 0}, .extent = extent};
                    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
                    renderer->render(command_buffer, *draw_data);
                };
                context.last_color_pass = pass->name;
            });
    }

    impl_->session = session.get();
    std::unique_ptr<ISimulationSession> simulation_session = std::move(session);
    if (auto result = simulation_.attach_session(std::move(simulation_session)); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(simulation session)");
        return error;
    }
    impl_->world_session = std::unique_ptr<ClientWorldSession>(new ClientWorldSession(*this));
    if (auto result = impl_->session->create_client_world(*impl_->world_session); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::init(create_client_world)");
        return error;
    }

    SNT_LOG_INFO("Client runtime initialized");
    return {};
}

void ClientRuntime::run() {
    if (!impl_ || !impl_->session || !impl_->world_session) return;

    auto last_time = simulation_.clock().now();
    while (!simulation_.stop_requested()) {
        // Input edge state must be cleared before SDL pumps this frame's
        // events. UI is built later in the same iteration and therefore sees
        // the exact snapshot already consumed by gameplay systems.
        impl_->input_system.new_frame();
        if (!impl_->window.poll_events()) break;

        const auto now = simulation_.clock().now();
        const float frame_ms = simulation_.clock().delta_since(last_time).count();
        const float delta_seconds = frame_ms / 1000.0f;
        last_time = now;
        impl_->fps_tracker.tick(frame_ms);

        impl_->input_system.end_frame();
        const auto& simulation_stats = simulation_.stats();
        impl_->stats = {
            .fps = impl_->fps_tracker.fps(),
            .frame_ms = impl_->fps_tracker.last_frame_ms,
            .tps = simulation_stats.tps,
            .mspt = simulation_stats.mspt,
            .job_workers = simulation_stats.job_workers,
        };
        ClientFrameContext frame_context(*this, simulation_.services(), *impl_->world_session,
                                         impl_->stats, delta_seconds);
        impl_->session->frame(frame_context);

        if (auto result = simulation_.advance_time(snt::core::DurationMs(frame_ms)); !result) {
            SNT_LOG_ERROR("Client fixed-tick scheduler failed; ending runtime loop: %s",
                          result.error().format().c_str());
            request_stop();
            break;
        }
        impl_->ui_draw_data = {};
        const auto& extent = impl_->vk_swapchain.extent();
        const auto window_metrics = impl_->window.metrics();
        const snt::ui::UiViewport ui_viewport{
            .framebuffer_size = {static_cast<float>(extent.width),
                                 static_cast<float>(extent.height)},
            .window_size = {static_cast<float>(window_metrics.window_size.width),
                            static_cast<float>(window_metrics.window_size.height)},
            .dpi_scale = window_metrics.display_scale,
            .user_scale = impl_->ui_user_scale,
        };
        ClientUiContext ui_context(*this, simulation_.services(), *impl_->world_session,
                                   ui_viewport);
        impl_->session->build_ui(ui_context);
        ui_context.flush();

        if (impl_->mui_renderer) {
            if (auto result = impl_->mui_renderer->synchronize_atlases(impl_->ui_draw_data); !result) {
                SNT_LOG_ERROR("MUI atlas synchronization failed: %s",
                              result.error().format().c_str());
            }
            impl_->mui_renderer->update_ortho(extent.width, extent.height);
        }

        impl_->render_system.update(simulation_.world_session().world(), delta_seconds);
        if (impl_->render_system.needs_resize()) {
            impl_->vk_device.wait_idle();
            const auto new_metrics = impl_->window.metrics();
            const auto new_size = new_metrics.pixel_size;
            if (new_size.width <= 0 || new_size.height <= 0) {
                SNT_LOG_INFO("Deferred swapchain recreation while the client window is minimized");
            } else if (impl_->vk_swapchain.recreate(static_cast<uint32_t>(new_size.width),
                                                     static_cast<uint32_t>(new_size.height))) {
                impl_->vk_depth.recreate(impl_->vk_swapchain);
                auto& world = simulation_.world_session().world();
                if (impl_->active_camera != entt::null &&
                    world.registry().all_of<snt::render::Camera>(impl_->active_camera)) {
                    auto& camera = world.registry().get<snt::render::Camera>(impl_->active_camera);
                    camera.aspect = static_cast<float>(new_size.width) /
                                    static_cast<float>(new_size.height);
                }
                SNT_LOG_INFO("Swapchain recreated: framebuffer=%dx%d dpi=%.2f",
                             new_size.width, new_size.height, new_metrics.display_scale);
            }
        }
    }

    impl_->vk_device.wait_idle();
}

void ClientRuntime::shutdown() {
    if (!impl_) {
        simulation_.shutdown();
        return;
    }

    simulation_.request_stop();
    // Session shutdown must precede all event-system and GPU teardown; the
    // game may still release script/UI state while those services are valid.
    simulation_.shutdown_session();
    simulation_.shutdown_execution();
    impl_->runtime_chunk_render_system.reset();

    impl_->vk_device.wait_idle();
    impl_->render_system.destroy_render_graph();
    if (impl_->chunk_renderer) {
        impl_->chunk_renderer->destroy();
        impl_->chunk_renderer.reset();
    }
    if (impl_->mui_renderer) {
        impl_->mui_renderer->destroy();
        impl_->mui_renderer.reset();
    }
    impl_->ui_runtime.reset();
    impl_->world_session.reset();

    impl_->vk_frame.destroy();
    impl_->vk_pipeline.destroy();
    impl_->vk_descriptor.destroy();
    impl_->vk_depth.destroy();
    impl_->vk_swapchain.destroy();
    impl_->asset_manager.shutdown();
    impl_->vk_device.destroy();
    if (impl_->vk_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(impl_->vk_instance.handle(), impl_->vk_surface, nullptr);
        impl_->vk_surface = VK_NULL_HANDLE;
    }
    impl_->vk_instance.destroy();
    impl_->window.destroy();

    SNT_LOG_INFO("Client runtime shutdown complete");
    impl_->session = nullptr;
    simulation_.shutdown_services();
    impl_.reset();
}

void ClientRuntime::request_stop() noexcept { simulation_.request_stop(); }

SimulationWorldSession& ClientWorldSession::simulation() const noexcept {
    return runtime_->simulation_.world_session();
}
snt::ecs::World& ClientWorldSession::world() const noexcept { return simulation().world(); }
snt::voxel::ChunkRegistry& ClientWorldSession::chunks() const noexcept {
    return simulation().chunks();
}
snt::ecs::EventBus& ClientWorldSession::events() const noexcept { return simulation().events(); }
snt::assets::AssetManager& ClientWorldSession::assets() const noexcept {
    return runtime_->impl_->asset_manager;
}
snt::ui::UiImageRegistry& ClientWorldSession::ui_images() const noexcept {
    return runtime_->impl_->ui_runtime->images();
}
snt::ui::UiLayerStack& ClientWorldSession::ui_layers() const noexcept {
    return runtime_->impl_->ui_runtime->layers();
}
snt::input::InputSystem& ClientWorldSession::input() const noexcept {
    return runtime_->impl_->input_system;
}
snt::voxel::ChunkRenderSystem& ClientWorldSession::chunk_render_system() const noexcept {
    return *runtime_->impl_->runtime_chunk_render_system;
}
snt::core::Expected<void> ClientWorldSession::set_active_camera(snt::ecs::EntityGuid guid) {
    return runtime_->set_active_camera(guid);
}
snt::core::Expected<void> ClientWorldSession::set_mouse_locked(bool locked) {
    return runtime_->set_mouse_locked(locked);
}

const snt::input::InputState& ClientFrameContext::input() const noexcept {
    return runtime_->impl_->input_system.state();
}
bool ClientFrameContext::mouse_locked() const noexcept { return runtime_->mouse_locked(); }
void ClientFrameContext::set_mouse_locked(bool locked) {
    if (auto result = runtime_->set_mouse_locked(locked); !result) {
        SNT_LOG_WARN("Client frame mouse-lock update failed: %s", result.error().format().c_str());
    }
}

bool ClientUiContext::mouse_locked() const noexcept { return runtime_->mouse_locked(); }
snt::ui::UiImageRegistry& ClientUiContext::images() const noexcept {
    return runtime_->impl_->ui_runtime->images();
}
snt::ui::UiLayerStack& ClientUiContext::layers() const noexcept {
    return runtime_->impl_->ui_runtime->layers();
}

void ClientUiContext::submit(snt::ui::Arc2DCommandBuffer commands,
                             snt::ui::UiLayer layer) {
    Submission submission;
    submission.layer = layer;
    submission.input_policy = snt::ui::ui_layer_input_policy(layer);
    submission.order = next_submission_order_++;
    submission.commands = std::make_unique<snt::ui::Arc2DCommandBuffer>(std::move(commands));
    submissions_.push_back(std::move(submission));
}

void ClientUiContext::flush() {
    if (!runtime_ || !runtime_->impl_) return;
    auto& impl = *runtime_->impl_;
    if (impl.ui_runtime) {
        impl.ui_runtime->set_viewport(viewport_);
        const snt::ui::UiScreenFrameContext frame_context{
            .viewport = viewport_.logical_size(),
            .ui_viewport = viewport_,
            .images = impl.ui_runtime->images(),
        };
        const auto& extension_screens = impl.ui_runtime->layers().prepare_frame(frame_context);
        for (const std::string& root_id : impl.ui_runtime->layers().take_invalidated_root_ids()) {
            impl.ui_runtime->cancel_interaction_for_root(root_id);
        }
        for (const snt::ui::UiScreenSubmission& screen : extension_screens) {
            if (!screen.root) continue;
            Submission submission;
            submission.layer = screen.layer;
            submission.input_policy = screen.input_policy;
            submission.order = next_submission_order_++;
            submission.borrowed_root = screen.root;
            submissions_.push_back(std::move(submission));
        }
    }
    const bool has_interactive_root = std::any_of(
        submissions_.begin(), submissions_.end(), [](const Submission& submission) {
            return submission.view_root() != nullptr;
        });
    if (!has_interactive_root) {
        if (impl.ui_runtime) impl.ui_runtime->clear_interaction_state();
        // Arc2D-only HUD commands still need to render, so continue through
        // the paint pass with an empty retained interaction tree.
    }

    std::stable_sort(submissions_.begin(), submissions_.end(),
                     [](const Submission& left, const Submission& right) {
                         const auto left_layer = static_cast<uint8_t>(left.layer);
                         const auto right_layer = static_cast<uint8_t>(right.layer);
                         return left_layer == right_layer
                             ? left.order < right.order
                             : left_layer < right_layer;
                     });

    if (impl.ui_runtime) {
        for (Submission& submission : submissions_) {
            if (snt::ui::View* root = submission.view_root()) {
                impl.ui_runtime->layout(*root, viewport_.logical_size());
            }
        }

        impl.ui_runtime->begin_input_frame(make_ui_input_state(
            impl.input_system.state(), viewport_, !runtime_->mouse_locked()));

        std::string focus_scope_root;
        for (auto it = submissions_.rbegin(); it != submissions_.rend(); ++it) {
            if (it->input_policy.blocks_keyboard_below && it->view_root()) {
                focus_scope_root = it->view_root()->id();
                break;
            }
        }
        impl.ui_runtime->set_focus_scope(std::move(focus_scope_root));

        for (auto it = submissions_.rbegin(); it != submissions_.rend(); ++it) {
            snt::ui::View* const root = it->view_root();
            const bool claimed = it->input_policy.accepts_pointer && root &&
                impl.ui_runtime->dispatch_pointer_input(*root);
            if (claimed || it->input_policy.blocks_pointer_below) break;
        }

        for (auto it = submissions_.rbegin(); it != submissions_.rend(); ++it) {
            snt::ui::View* const root = it->view_root();
            const bool claimed = it->input_policy.accepts_keyboard && root &&
                impl.ui_runtime->dispatch_keyboard_input(*root);
            if (claimed || it->input_policy.blocks_keyboard_below) break;
        }

        impl.ui_runtime->end_input_frame();

        for (Submission& submission : submissions_) {
            if (snt::ui::View* root = submission.view_root()) {
                impl.ui_runtime->synchronize_interaction_state(*root);
            }
        }
    }

    std::optional<snt::ui::Rect> text_input_bounds;
    if (impl.ui_runtime && !runtime_->mouse_locked()) {
        for (Submission& submission : submissions_) {
            snt::ui::View* const root = submission.view_root();
            if (!root) continue;
            text_input_bounds = impl.ui_runtime->focused_text_input_bounds(*root);
            if (text_input_bounds) break;
        }
    }
    if (auto result = impl.window.set_text_input_active(text_input_bounds.has_value()); !result) {
        SNT_LOG_WARN("Platform text input activation failed: %s", result.error().format().c_str());
    } else if (text_input_bounds && impl.window.text_input_active()) {
        const snt::ui::Vec2 top_left = viewport_.logical_to_window(text_input_bounds->pos);
        const snt::ui::Vec2 bottom_right = viewport_.logical_to_window({
            .x = text_input_bounds->pos.x + text_input_bounds->size.x,
            .y = text_input_bounds->pos.y + text_input_bounds->size.y,
        });
        const int left = static_cast<int>(std::floor(top_left.x));
        const int top = static_cast<int>(std::floor(top_left.y));
        const int right = static_cast<int>(std::ceil(bottom_right.x));
        const int bottom = static_cast<int>(std::ceil(bottom_right.y));
        if (auto result = impl.window.set_text_input_area({
                .x = left,
                .y = top,
                .width = std::max(1, right - left),
                .height = std::max(1, bottom - top),
            }); !result) {
            SNT_LOG_WARN("Platform text input area update failed: %s",
                         result.error().format().c_str());
        }
    }

    for (Submission& submission : submissions_) {
        if (snt::ui::View* root = submission.view_root(); root && impl.ui_runtime) {
            auto frame = impl.ui_runtime->paint(*root);
            append_draw_data(impl.ui_draw_data, frame.draw_data);
        } else if (submission.commands && impl.ui_runtime) {
            append_draw_data(impl.ui_draw_data,
                             impl.ui_runtime->build_draw_data(*submission.commands));
        }
    }
    submissions_.clear();
}

bool ClientRuntime::mouse_locked() const noexcept { return impl_ && impl_->mouse_locked; }

snt::core::Expected<void> ClientRuntime::set_mouse_locked(bool locked) {
    if (!impl_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ClientRuntime is not initialized"};
    }
    if (impl_->mouse_locked == locked) return {};
    if (auto result = impl_->window.set_relative_mouse_mode(locked); !result) {
        auto error = result.error();
        error.with_context("ClientRuntime::set_mouse_locked");
        return error;
    }
    impl_->mouse_locked = locked;
    auto& events = simulation_.world_session().events();
    events.enqueue<snt::core::MouseLockChanged>({locked});
    events.update();
    SNT_LOG_INFO("Mouse lock %s", locked ? "enabled" : "disabled");
    return {};
}

snt::core::Expected<void> ClientRuntime::set_active_camera(snt::ecs::EntityGuid guid) {
    if (!impl_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ClientRuntime is not initialized"};
    }
    auto& world = simulation_.world_session().world();
    const entt::entity entity = world.find_entity_by_guid(guid);
    if (entity == entt::null ||
        !world.registry().all_of<snt::render::Transform, snt::render::Camera>(entity)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Active camera Guid does not identify a camera entity"};
    }
    impl_->active_camera = entity;
    impl_->render_system.set_active_camera(entity);
    return {};
}

}  // namespace snt::engine
