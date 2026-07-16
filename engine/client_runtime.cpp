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
#include <chrono>
#include <memory>
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

void append_draw_data(snt::ui::UiDrawData& destination,
                      const snt::ui::UiDrawData& source) {
    if (source.vertices.empty() || source.indices.empty()) return;

    if (source.glyph_atlas) {
        if (!destination.glyph_atlas) {
            destination.glyph_atlas = source.glyph_atlas;
        } else if (destination.glyph_atlas.get() != source.glyph_atlas.get()) {
            SNT_LOG_ERROR("MUI draw batches reference different glyph atlases; batch rejected");
            return;
        }
    }

    if (destination.vertices.size() + source.vertices.size() > 0xFFFFu) {
        SNT_LOG_WARN("UI draw data overflow while appending; dropping appended batch");
        return;
    }

    const uint16_t base = static_cast<uint16_t>(destination.vertices.size());
    destination.vertices.insert(destination.vertices.end(), source.vertices.begin(), source.vertices.end());
    destination.indices.reserve(destination.indices.size() + source.indices.size());
    for (const uint16_t index : source.indices) {
        destination.indices.push_back(static_cast<uint16_t>(base + index));
    }
}

snt::ui::UiInputState make_ui_input_state(const snt::input::InputState& input,
                                          bool ui_input_enabled) {
    snt::ui::UiInputState result;
    result.pointer_enabled = ui_input_enabled;
    result.pointer_position = {
        static_cast<float>(input.mouse_x),
        static_cast<float>(input.mouse_y),
    };
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
    add_key(SDL_SCANCODE_LEFT, snt::ui::UiKey::Left);
    add_key(SDL_SCANCODE_RIGHT, snt::ui::UiKey::Right);
    add_key(SDL_SCANCODE_UP, snt::ui::UiKey::Up);
    add_key(SDL_SCANCODE_DOWN, snt::ui::UiKey::Down);
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
    snt::ui::Arc2DRenderer arc2d_renderer;
    snt::ui::UiDrawData ui_draw_data;

    FpsTracker fps_tracker;
    ClientRuntimeStats stats;
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
    const auto window_size = impl_->window.size();
    SNT_LOG_INFO("Client window created: %dx%d", window_size.width, window_size.height);

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
    if (auto result = impl_->vk_swapchain.init(impl_->vk_device,
                                                static_cast<uint32_t>(window_size.width),
                                                static_cast<uint32_t>(window_size.height)); !result) {
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
        ClientUiContext ui_context(*this, simulation_.services(), *impl_->world_session,
                                   static_cast<float>(extent.width),
                                   static_cast<float>(extent.height));
        impl_->session->build_ui(ui_context);
        ui_context.flush();

        if (impl_->mui_renderer) {
            if (auto result = impl_->mui_renderer->synchronize_glyph_atlas(impl_->ui_draw_data); !result) {
                SNT_LOG_ERROR("MUI glyph atlas synchronization failed: %s",
                              result.error().format().c_str());
            }
            impl_->mui_renderer->update_ortho(extent.width, extent.height);
        }

        impl_->render_system.update(simulation_.world_session().world(), delta_seconds);
        if (impl_->render_system.needs_resize()) {
            impl_->vk_device.wait_idle();
            const auto new_size = impl_->window.size();
            if (impl_->vk_swapchain.recreate(static_cast<uint32_t>(new_size.width),
                                             static_cast<uint32_t>(new_size.height))) {
                impl_->vk_depth.recreate(impl_->vk_swapchain);
                auto& world = simulation_.world_session().world();
                if (impl_->active_camera != entt::null &&
                    world.registry().all_of<snt::render::Camera>(impl_->active_camera)) {
                    auto& camera = world.registry().get<snt::render::Camera>(impl_->active_camera);
                    camera.aspect = static_cast<float>(new_size.width) /
                                    static_cast<float>(new_size.height);
                }
                SNT_LOG_INFO("Swapchain recreated: %dx%d", new_size.width, new_size.height);
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

void ClientUiContext::submit(std::unique_ptr<snt::ui::View> root,
                             snt::ui::UiLayer layer) {
    if (!root) {
        SNT_LOG_WARN("Client UI ignored a null view-root submission");
        return;
    }
    Submission submission;
    submission.layer = layer;
    submission.order = next_submission_order_++;
    submission.root = std::move(root);
    submissions_.push_back(std::move(submission));
}

void ClientUiContext::submit(snt::ui::Arc2DCommandBuffer commands,
                             snt::ui::UiLayer layer) {
    Submission submission;
    submission.layer = layer;
    submission.order = next_submission_order_++;
    submission.commands = std::make_unique<snt::ui::Arc2DCommandBuffer>(std::move(commands));
    submissions_.push_back(std::move(submission));
}

void ClientUiContext::flush() {
    if (!runtime_ || !runtime_->impl_) return;
    auto& impl = *runtime_->impl_;
    if (submissions_.empty()) {
        if (impl.ui_runtime) impl.ui_runtime->clear_interaction_state();
        return;
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
            if (submission.root) {
                impl.ui_runtime->layout(*submission.root,
                                        {viewport_width_, viewport_height_});
            }
        }

        impl.ui_runtime->begin_input_frame(make_ui_input_state(
            impl.input_system.state(), !runtime_->mouse_locked()));

        bool pointer_claimed = false;
        for (auto it = submissions_.rbegin(); it != submissions_.rend() && !pointer_claimed; ++it) {
            if (it->root) pointer_claimed = impl.ui_runtime->dispatch_pointer_input(*it->root);
        }

        bool keyboard_claimed = false;
        for (auto it = submissions_.rbegin(); it != submissions_.rend() && !keyboard_claimed; ++it) {
            if (it->root) keyboard_claimed = impl.ui_runtime->dispatch_keyboard_input(*it->root);
        }

        for (Submission& submission : submissions_) {
            if (submission.root) impl.ui_runtime->synchronize_interaction_state(*submission.root);
        }
    }

    for (Submission& submission : submissions_) {
        if (submission.root && impl.ui_runtime) {
            auto frame = impl.ui_runtime->paint(*submission.root);
            append_draw_data(impl.ui_draw_data, frame.draw_data);
        } else if (submission.commands) {
            append_draw_data(impl.ui_draw_data,
                             impl.arc2d_renderer.build_draw_data(*submission.commands));
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
