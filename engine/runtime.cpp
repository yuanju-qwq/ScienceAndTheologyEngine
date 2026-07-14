// Runtime implementation.

#define SNT_LOG_CHANNEL "runtime"
#include "engine/runtime.h"

#include "engine/game_session.h"
#include "engine/runtime_observer.h"
#include "engine/runtime_services.h"

#include "assets/asset_catalog.h"
#include "assets/asset_manager.h"
#include "assets/filesystem_asset_source.h"
#include "core/events.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/memory_tracker.h"
#include "core/path_utils.h"
#include "core/profiling.h"
#include "render/render_components.h"
#include "ecs/event_bus.h"
#include "ecs/system_scheduler.h"
#include "ecs/world.h"
#include "input/input_system.h"
#include "platform/window.h"
#include "render/render_system.h"
#include "render_backend/command_context.h"
#include "render_backend/vertex_buffer_pool.h"
#include "render_backend/vulkan_buffer.h"
#include "render_backend/vulkan_depth.h"
#include "render_backend/vulkan_descriptor.h"
#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_frame.h"
#include "render_backend/vulkan_instance.h"
#include "render_backend/vulkan_mesh.h"
#include "render_backend/vulkan_pipeline.h"
#include "render_backend/vulkan_swapchain.h"
#include "script/script_manager.h"
#include "ui/mui_renderer.h"
#include "ui/retained_mui.h"
#include "voxel/chunk_renderer.h"
#include "voxel/chunk_render_system.h"
#include "voxel/data/chunk_registry.h"

#include <volk.h>

#include <chrono>
#include <filesystem>
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

struct TickStats {
    static constexpr float kTickRate = 20.0f;
    static constexpr float kTickMs = 1000.0f / kTickRate;
    static constexpr int kMaxCatchup = 5;

    float accumulator = 0.0f;
    float last_tick_ms = 0.0f;
    int ticks_this_second = 0;
    float second_timer = 0.0f;
    float tps = 0.0f;
    uint32_t dropped_debt_this_second = 0;

    template <typename TickFn>
    void consume(float frame_ms, TickFn tick_fn) {
        accumulator += frame_ms;
        second_timer += frame_ms;

        int ticks_run = 0;
        while (accumulator >= kTickMs && ticks_run < kMaxCatchup) {
            const auto started = std::chrono::high_resolution_clock::now();
            tick_fn();
            const auto finished = std::chrono::high_resolution_clock::now();
            last_tick_ms = static_cast<float>(
                std::chrono::duration<double, std::milli>(finished - started).count());
            accumulator -= kTickMs;
            ++ticks_this_second;
            ++ticks_run;
        }

        if (ticks_run >= kMaxCatchup && accumulator >= kTickMs) {
            accumulator = 0.0f;
            ++dropped_debt_this_second;
        }

        if (second_timer >= 1000.0f) {
            tps = static_cast<float>(ticks_this_second);
            if (dropped_debt_this_second > 0) {
                SNT_LOG_WARN("Fixed tick catch-up cap dropped time debt %u time(s) in the last second",
                             dropped_debt_this_second);
            }
            ticks_this_second = 0;
            dropped_debt_this_second = 0;
            second_timer -= 1000.0f;
        }
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

}  // namespace

struct Runtime::Impl {
    snt::platform::Window window;
    snt::input::InputSystem input_system;
    snt::ecs::EventBus event_bus;

    snt::core::RuntimeConfig config;
    std::optional<snt::core::RuntimePathResolver> paths;
    snt::core::RealClock real_clock;
    snt::core::IClock* clock = &real_clock;

    snt::render_backend::VulkanInstance vk_instance;
    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    snt::render_backend::VulkanDevice vk_device;
    snt::render_backend::VulkanSwapchain vk_swapchain;
    snt::render_backend::VulkanDepth vk_depth;
    snt::render_backend::VulkanDescriptor vk_descriptor;
    snt::render_backend::VulkanPipeline vk_pipeline;
    snt::render_backend::VulkanFrame vk_frame;
    std::optional<snt::assets::FilesystemAssetSource> asset_source;
    std::optional<snt::assets::AssetCatalog> asset_catalog;
    snt::assets::AssetManager asset_manager;
    snt::script::ScriptManager script_manager;

    snt::ecs::World world;
    entt::entity active_camera = entt::null;
    snt::render::RenderSystem render_system;

    snt::voxel::ChunkRegistry chunk_registry;
    std::unique_ptr<snt::voxel::ChunkRenderer> chunk_renderer;
    std::shared_ptr<snt::voxel::ChunkRenderSystem> runtime_chunk_render_system;

    std::unique_ptr<snt::ui::MuiRenderer> mui_renderer;
    std::unique_ptr<snt::ui::UiRuntime> ui_runtime;
    snt::ui::Arc2DRenderer arc2d_renderer;
    snt::ui::UiDrawData ui_draw_data;

    FpsTracker fps_tracker;
    TickStats tick_stats;
    RuntimeStats stats;
    uint64_t tick_index = 0;
    bool mouse_locked = false;

    snt::core::JobSystemP2 job_system;
    std::unique_ptr<snt::ecs::SystemScheduler> system_scheduler;
    std::unique_ptr<RuntimeServices> services;
    std::unique_ptr<WorldSession> world_session;
    std::unique_ptr<IGameSession> session;
    bool session_started = false;
};

RuntimeServices::RuntimeServices(const snt::core::RuntimeConfig& config,
                                 const snt::core::RuntimePathResolver& paths,
                                 snt::core::IClock& clock,
                                 snt::core::Logger& logger,
                                 snt::core::JobSystem& jobs,
                                 snt::assets::AssetManager& assets,
                                 snt::assets::IAssetSource& asset_source,
                                 const snt::assets::AssetCatalog& asset_catalog,
                                 snt::script::ScriptManager& scripts)
    : config_(&config), paths_(&paths), clock_(&clock), logger_(&logger), jobs_(&jobs),
      assets_(&assets), asset_source_(&asset_source), asset_catalog_(&asset_catalog),
      scripts_(&scripts) {}

const snt::core::RuntimeConfig& RuntimeServices::config() const noexcept { return *config_; }
const snt::core::RuntimePathResolver& RuntimeServices::paths() const noexcept { return *paths_; }
snt::core::IClock& RuntimeServices::clock() const noexcept { return *clock_; }
snt::core::Logger& RuntimeServices::logger() const noexcept { return *logger_; }
snt::core::JobSystem& RuntimeServices::jobs() const noexcept { return *jobs_; }
snt::assets::AssetManager& RuntimeServices::assets() const noexcept { return *assets_; }
snt::assets::IAssetSource& RuntimeServices::asset_source() const noexcept {
    return *asset_source_;
}
const snt::assets::AssetCatalog& RuntimeServices::asset_catalog() const noexcept {
    return *asset_catalog_;
}
snt::script::ScriptManager& RuntimeServices::scripts() const noexcept { return *scripts_; }

snt::ecs::World& WorldSession::world() const noexcept { return runtime_->impl_->world; }
snt::voxel::ChunkRegistry& WorldSession::chunks() const noexcept {
    return runtime_->impl_->chunk_registry;
}
snt::voxel::ChunkRenderSystem& WorldSession::chunk_render_system() const noexcept {
    return *runtime_->impl_->runtime_chunk_render_system;
}
snt::ecs::EventBus& WorldSession::events() const noexcept { return runtime_->impl_->event_bus; }
snt::input::InputSystem& WorldSession::input() const noexcept {
    return runtime_->impl_->input_system;
}

snt::core::Expected<snt::ecs::SystemHandle> WorldSession::register_main_system(
    std::shared_ptr<snt::ecs::System> system) {
    if (!runtime_ || !runtime_->impl_ || !runtime_->impl_->system_scheduler) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "WorldSession scheduler is unavailable"};
    }
    return runtime_->impl_->system_scheduler->register_main(std::move(system));
}

snt::core::Expected<snt::ecs::SystemHandle> WorldSession::register_worker_system(
    std::shared_ptr<snt::ecs::IWorkerSystem> system) {
    if (!runtime_ || !runtime_->impl_ || !runtime_->impl_->system_scheduler) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "WorldSession scheduler is unavailable"};
    }
    return runtime_->impl_->system_scheduler->register_worker(std::move(system));
}

snt::core::Expected<void> WorldSession::set_system_enabled(
    snt::ecs::SystemHandle handle, bool enabled) {
    if (!runtime_ || !runtime_->impl_ || !runtime_->impl_->system_scheduler) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "WorldSession scheduler is unavailable"};
    }
    return runtime_->impl_->system_scheduler->set_enabled(handle, enabled);
}

snt::core::Expected<void> WorldSession::set_active_camera(snt::ecs::EntityGuid guid) {
    return runtime_->set_active_camera(guid);
}

void WorldSession::set_mouse_locked(bool locked) { runtime_->set_mouse_locked(locked); }

const snt::input::InputState& FrameContext::input() const noexcept {
    return runtime_->impl_->input_system.state();
}

bool FrameContext::mouse_locked() const noexcept { return runtime_->mouse_locked(); }
void FrameContext::set_mouse_locked(bool locked) { runtime_->set_mouse_locked(locked); }

bool UiContext::mouse_locked() const noexcept { return runtime_->mouse_locked(); }
void UiContext::submit(snt::ui::View& root) { runtime_->submit_ui(root); }
void UiContext::submit(const snt::ui::Arc2DCommandBuffer& commands) {
    runtime_->submit_ui(commands);
}

Runtime::Runtime() : impl_(std::make_unique<Impl>()) {}
Runtime::~Runtime() { shutdown(); }

snt::core::Expected<void> Runtime::init(const snt::core::RuntimeConfig& config,
                                         snt::core::RuntimePaths runtime_paths,
                                         std::unique_ptr<IGameSession> session) {
    using namespace snt::platform;
    using namespace snt::render_backend;

    if (!session) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Runtime::init requires an IGameSession"};
    }

    SNT_LOG_INFO("Starting SNT runtime");
    impl_->config = config;

    auto resolved_paths = snt::core::RuntimePathResolver::create(std::move(runtime_paths));
    if (!resolved_paths) {
        auto error = resolved_paths.error();
        error.with_context("Runtime::init(RuntimePaths)");
        return error;
    }
    impl_->paths.emplace(std::move(*resolved_paths));

    impl_->job_system.init();
    SNT_LOG_INFO("Job system started: %d workers", impl_->job_system.worker_count());
    impl_->system_scheduler = std::make_unique<snt::ecs::SystemScheduler>(impl_->job_system);

    {
        const std::string log_path = impl_->paths->resolve_user("logs/engine.log");
        try {
            std::filesystem::create_directories(std::filesystem::path(log_path).parent_path());
        } catch (const std::exception& error) {
            SNT_LOG_WARN("Failed to create log directory: %s", error.what());
        }
        if (!snt::core::Logger::instance().add_file_sink(log_path.c_str())) {
            SNT_LOG_WARN("Failed to open log file '%s'; logging to stderr only", log_path.c_str());
        } else {
            SNT_LOG_INFO("Logging to file: %s", log_path.c_str());
        }
    }

    auto asset_source = snt::assets::FilesystemAssetSource::create(
        std::filesystem::path(impl_->paths->roots().game_root));
    if (!asset_source) {
        auto error = asset_source.error();
        error.with_context("Runtime::init(FilesystemAssetSource)");
        return error;
    }
    impl_->asset_source.emplace(std::move(*asset_source));

    auto asset_catalog = snt::assets::AssetCatalog::load(
        *impl_->asset_source,
        snt::assets::AssetSourceRequest{.requested_path = config.assets.manifest_path});
    if (!asset_catalog) {
        auto error = asset_catalog.error();
        error.with_context("Runtime::init(AssetCatalog)");
        return error;
    }
    impl_->asset_catalog.emplace(std::move(*asset_catalog));

    if (auto result = impl_->window.create(WindowDesc{
            .title = config.window.title,
            .width = config.window.width,
            .height = config.window.height,
            .resizable = config.window.resizable,
            .vulkan_enabled = config.window.vulkan_enabled,
        }); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(window)");
        return error;
    }
    const auto window_size = impl_->window.size();
    SNT_LOG_INFO("Window created: %dx%d", window_size.width, window_size.height);

    impl_->event_bus.sink<snt::core::SdlEventFired>()
        .connect<&snt::input::InputSystem::on_sdl_event>(&impl_->input_system);
    impl_->window.set_event_callback([this](const void* sdl_event) {
        impl_->event_bus.enqueue<snt::core::SdlEventFired>({sdl_event});
        impl_->event_bus.update();
    });

    if (auto result = impl_->vk_instance.init(impl_->window); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(vk_instance)");
        return error;
    }

    uint64_t surface_bits = 0;
    if (auto result = impl_->window.create_vulkan_surface(
            reinterpret_cast<void*>(impl_->vk_instance.handle()), &surface_bits); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(vk_surface)");
        return error;
    }
    impl_->vk_surface = reinterpret_cast<VkSurfaceKHR>(surface_bits);

    if (auto result = impl_->vk_device.init(impl_->vk_instance.handle(), impl_->vk_surface); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(vk_device)");
        return error;
    }
    if (auto result = impl_->asset_manager.init(
            &impl_->vk_device, *impl_->asset_source, *impl_->asset_catalog); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(AssetManager)");
        return error;
    }
    if (auto result = impl_->vk_swapchain.init(impl_->vk_device,
                                                static_cast<uint32_t>(window_size.width),
                                                static_cast<uint32_t>(window_size.height)); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(vk_swapchain)");
        return error;
    }
    if (auto result = impl_->vk_depth.init(impl_->vk_device, impl_->vk_swapchain); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(vk_depth)");
        return error;
    }
    if (auto result = impl_->vk_descriptor.init(impl_->vk_device, config.render.max_entities); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(vk_descriptor)");
        return error;
    }
    if (auto result = impl_->vk_pipeline.init(
            impl_->vk_device, impl_->vk_descriptor, impl_->vk_swapchain.image_format(),
            impl_->vk_depth.format(), impl_->paths->resolve_engine(config.render.vert_shader_path),
            impl_->paths->resolve_engine(config.render.frag_shader_path),
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
        error.with_context("Runtime::init(vk_pipeline)");
        return error;
    }
    if (auto result = impl_->vk_frame.init(
            impl_->vk_device, static_cast<uint32_t>(impl_->vk_swapchain.image_views().size())); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(vk_frame)");
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
        error.with_context("Runtime::init(render_graph)");
        return error;
    }

    impl_->chunk_renderer = std::make_unique<snt::voxel::ChunkRenderer>();
    if (auto result = impl_->chunk_renderer->init(
            impl_->vk_device, *impl_->paths, impl_->vk_swapchain.image_format(), impl_->vk_depth.format(),
            impl_->paths->resolve_engine("shaders/voxel.vert.spv"),
            impl_->paths->resolve_engine("shaders/voxel.frag.spv"),
            config.voxel.max_chunks); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(chunk_renderer)");
        return error;
    }
    auto chunk_system = std::make_shared<snt::voxel::ChunkRenderSystem>(impl_->job_system);
    chunk_system->set_chunk_renderer(impl_->chunk_renderer.get());
    chunk_system->set_chunk_registry(&impl_->chunk_registry);
    chunk_system->set_remesh_jobs_per_frame(config.voxel.remesh_jobs_per_frame);
    chunk_system->set_uploads_per_frame(config.voxel.uploads_per_frame);
    if (auto result = impl_->system_scheduler->register_main(chunk_system); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(register ChunkRenderSystem)");
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
            if (!context.last_color_pass.empty()) {
                pass->depends_on.push_back(context.last_color_pass);
            }
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
                VkViewport viewport{
                    .x = 0.0f,
                    .y = 0.0f,
                    .width = static_cast<float>(extent.width),
                    .height = static_cast<float>(extent.height),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                };
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
    impl_->ui_runtime = std::make_unique<snt::ui::UiRuntime>(*impl_->paths, std::move(text_config));
    if (!impl_->ui_runtime->text_available()) {
        SNT_LOG_ERROR("MUI text initialization failed: %s",
                      impl_->ui_runtime->text_initialization_error().c_str());
    }

    impl_->mui_renderer = std::make_unique<snt::ui::MuiRenderer>();
    if (auto result = impl_->mui_renderer->init(
            impl_->vk_device, impl_->vk_swapchain.image_format(), *impl_->paths);
        !result) {
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
                if (!context.last_color_pass.empty()) {
                    pass->depends_on.push_back(context.last_color_pass);
                }
                pass->color_attachments.push_back(snt::renderer::ColorAttachmentDecl{
                    .resource = context.color_resource,
                    .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                });

                const auto extent = context.extent;
                pass->execute = [renderer, draw_data, extent]
                                (snt::render_backend::CommandContext& pass_context) {
                    VkCommandBuffer command_buffer = pass_context.handle();
                    VkViewport viewport{
                        .x = 0.0f,
                        .y = 0.0f,
                        .width = static_cast<float>(extent.width),
                        .height = static_cast<float>(extent.height),
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f,
                    };
                    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
                    VkRect2D scissor{.offset = {0, 0}, .extent = extent};
                    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
                    renderer->render(command_buffer, *draw_data);
                };
                context.last_color_pass = pass->name;
            });
    }

    impl_->services = std::unique_ptr<RuntimeServices>(new RuntimeServices(
        impl_->config, *impl_->paths, *impl_->clock, snt::core::Logger::instance(),
        impl_->job_system, impl_->asset_manager,
        *impl_->asset_source, *impl_->asset_catalog,
        impl_->script_manager));
    impl_->world_session = std::unique_ptr<WorldSession>(new WorldSession(*this));
    impl_->session = std::move(session);
    impl_->session_started = true;

    if (auto result = impl_->session->register_content(*impl_->services); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(register_content)");
        return error;
    }
    if (auto result = impl_->session->create_world(*impl_->world_session); !result) {
        auto error = result.error();
        error.with_context("Runtime::init(create_world)");
        return error;
    }

    SNT_LOG_INFO("Runtime session initialized");
    snt::core::MemoryTracker::instance().log_stats();
    return {};
}

void Runtime::run() {
    if (!impl_ || !impl_->session || !impl_->services || !impl_->world_session) return;

    auto last_time = impl_->clock->now();
    while (impl_->window.poll_events()) {
        SNT_PROFILE_SCOPE("frame");
        const auto now = impl_->clock->now();
        const float frame_ms = impl_->clock->delta_since(last_time).count();
        const float delta_seconds = frame_ms / 1000.0f;
        last_time = now;
        impl_->fps_tracker.tick(frame_ms);

        impl_->input_system.end_frame();
        impl_->stats = {
            .fps = impl_->fps_tracker.fps(),
            .frame_ms = impl_->fps_tracker.last_frame_ms,
            .tps = impl_->tick_stats.tps,
            .mspt = impl_->tick_stats.last_tick_ms,
            .job_workers = impl_->job_system.worker_count(),
        };
        FrameContext frame_context(*this, *impl_->services, *impl_->world_session,
                                   impl_->stats, delta_seconds);
        impl_->session->frame(frame_context);

        constexpr float kFixedDeltaSeconds = TickStats::kTickMs / 1000.0f;
        if (!impl_->system_scheduler) {
            SNT_LOG_ERROR("Fixed-tick scheduler is unavailable; ending runtime loop");
            break;
        }
        bool scheduler_failed = false;
        impl_->tick_stats.consume(frame_ms, [&] {
            if (scheduler_failed) return;

            FixedTickContext tick_context(*impl_->services, *impl_->world_session,
                                          kFixedDeltaSeconds, ++impl_->tick_index);
            impl_->session->fixed_tick(tick_context);
            auto result = impl_->system_scheduler->fixed_tick(impl_->world, kFixedDeltaSeconds);
            if (!result) {
                SNT_LOG_ERROR("Fixed-tick scheduler failed; ending runtime loop: %s",
                              result.error().format().c_str());
                scheduler_failed = true;
            }
        });
        if (scheduler_failed) break;
        impl_->input_system.new_frame();

        impl_->ui_draw_data = {};
        const auto& extent = impl_->vk_swapchain.extent();
        UiContext ui_context(*this, *impl_->services, *impl_->world_session,
                             static_cast<float>(extent.width), static_cast<float>(extent.height));
        impl_->session->build_ui(ui_context);

        if (impl_->mui_renderer) {
            if (auto result = impl_->mui_renderer->synchronize_glyph_atlas(impl_->ui_draw_data); !result) {
                SNT_LOG_ERROR("MUI glyph atlas synchronization failed: %s",
                              result.error().format().c_str());
            }
            impl_->mui_renderer->update_ortho(extent.width, extent.height);
        }

        impl_->render_system.update(impl_->world, delta_seconds);
        if (impl_->render_system.needs_resize()) {
            impl_->vk_device.wait_idle();
            const auto new_size = impl_->window.size();
            if (impl_->vk_swapchain.recreate(static_cast<uint32_t>(new_size.width),
                                             static_cast<uint32_t>(new_size.height))) {
                impl_->vk_depth.recreate(impl_->vk_swapchain);
                if (impl_->active_camera != entt::null &&
                    impl_->world.registry().all_of<snt::render::Camera>(impl_->active_camera)) {
                    auto& camera = impl_->world.registry().get<snt::render::Camera>(impl_->active_camera);
                    camera.aspect = static_cast<float>(new_size.width) /
                                    static_cast<float>(new_size.height);
                }
                SNT_LOG_INFO("Swapchain recreated: %dx%d", new_size.width, new_size.height);
            }
        }
    }

    impl_->vk_device.wait_idle();
}

void Runtime::shutdown() {
    if (!impl_) return;

    if (impl_->session_started && impl_->session) {
        impl_->session->shutdown();
        impl_->session_started = false;
    }
    impl_->session.reset();

    // Event sinks can borrow system instances (for example, player input),
    // so disconnect them before releasing scheduler-owned systems.
    impl_->event_bus.clear();
    if (impl_->system_scheduler) {
        impl_->system_scheduler->shutdown();
        impl_->system_scheduler.reset();
    }
    impl_->runtime_chunk_render_system.reset();

    // Worker tasks can retain immutable chunk snapshots and result queues.
    // Join them before destroying the World or GPU resources they reference.
    impl_->job_system.shutdown();
    impl_->vk_device.wait_idle();

    // Sessions own script content; Runtime owns the manager lifetime.
    impl_->script_manager.shutdown();
    impl_->world_session.reset();
    impl_->services.reset();

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

    impl_->vk_frame.destroy();
    impl_->vk_pipeline.destroy();
    impl_->vk_descriptor.destroy();
    impl_->vk_depth.destroy();
    impl_->vk_swapchain.destroy();
    impl_->asset_manager.shutdown();
    impl_->asset_catalog.reset();
    impl_->asset_source.reset();

    impl_->vk_device.destroy();
    if (impl_->vk_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(impl_->vk_instance.handle(), impl_->vk_surface, nullptr);
        impl_->vk_surface = VK_NULL_HANDLE;
    }
    impl_->vk_instance.destroy();
    impl_->window.destroy();

    SNT_LOG_INFO("Runtime shutdown complete");
    impl_.reset();
    snt::core::MemoryTracker::instance().log_stats();
}

snt::core::IClock& Runtime::get_clock() { return *impl_->clock; }
snt::core::TimePoint Runtime::get_time() { return impl_->clock->now(); }

void Runtime::set_clock(snt::core::IClock* clock) {
    impl_->clock = clock != nullptr ? clock : &impl_->real_clock;
}

bool Runtime::mouse_locked() const noexcept { return impl_ && impl_->mouse_locked; }

void Runtime::set_mouse_locked(bool locked) {
    if (!impl_ || impl_->mouse_locked == locked) return;
    if (auto result = impl_->window.set_relative_mouse_mode(locked); !result) {
        SNT_LOG_WARN("Failed to %s mouse lock: %s", locked ? "enable" : "disable",
                     result.error().format().c_str());
        return;
    }
    impl_->mouse_locked = locked;
    impl_->event_bus.enqueue<snt::core::MouseLockChanged>({locked});
    impl_->event_bus.update();
    SNT_LOG_INFO("Mouse lock %s", locked ? "enabled" : "disabled");
}

snt::core::Expected<void> Runtime::set_active_camera(snt::ecs::EntityGuid guid) {
    if (!impl_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Runtime is not initialized"};
    }
    const entt::entity entity = impl_->world.find_entity_by_guid(guid);
    if (entity == entt::null ||
        !impl_->world.registry().all_of<snt::render::Transform, snt::render::Camera>(entity)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Active camera Guid does not identify a camera entity"};
    }
    impl_->active_camera = entity;
    impl_->render_system.set_active_camera(entity);
    return {};
}

void Runtime::submit_ui(snt::ui::View& root) {
    if (!impl_ || !impl_->ui_runtime) return;
    const auto& extent = impl_->vk_swapchain.extent();
    auto frame = impl_->ui_runtime->build_frame(
        root, {static_cast<float>(extent.width), static_cast<float>(extent.height)});
    append_draw_data(impl_->ui_draw_data, frame.draw_data);
}

void Runtime::submit_ui(const snt::ui::Arc2DCommandBuffer& commands) {
    if (!impl_) return;
    append_draw_data(impl_->ui_draw_data, impl_->arc2d_renderer.build_draw_data(commands));
}

}  // namespace snt::engine
