// Engine implementation.
//
// P2.B1: moves the per-frame loop + resource setup from main.cpp into
// the Engine class. Logic is preserved 1:1 from P1.5 + P2.A1 (input
// decoupling); the only structural change is ownership moves from local
// variables in main() into Engine::Impl.

#define SNT_LOG_CHANNEL "engine"
#include "core/log.h"

#include "engine.h"  // Engine class definition (PImpl)

#include "assets/asset_manager.h"  // AssetManager::init/shutdown + mesh_cache
#include "core/clock.h"           // RealClock (replaces raw std::chrono)
#include "core/events.h"          // SdlEventFired, MouseLockChanged
#include "core/job_system.h"
#include "core/memory_tracker.h"
#include "core/path_utils.h"      // path_utils::resolve for shader/asset paths
#include "core/profiling.h"
#include "data/world/chunk_registry.h"  // P3: ChunkRegistry
#include "engine/demo_world_bootstrap.h"
#include "ecs/components.h"
#include "ecs/entity_guid.h"
#include "ecs/event_bus.h"        // EventBus = entt::dispatcher
#include "ecs/world.h"
#include "input/input_system.h"
#include "platform/window.h"
#include "player/player_controller.h"
#include "render/render_system.h"
#include "scene/scene.h"          // load_scene for binary scene loading
#include "render_backend/command_context.h"
#include "render_backend/vulkan_buffer.h"        // P3: complete type for MSVC eager unique_ptr deleter instantiation
#include "render_backend/vulkan_depth.h"
#include "render_backend/vulkan_descriptor.h"
#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_frame.h"
#include "render_backend/vulkan_instance.h"
#include "render_backend/vulkan_mesh.h"
#include "render_backend/vulkan_pipeline.h"
#include "render_backend/vulkan_swapchain.h"
#include "render_backend/vertex_buffer_pool.h"  // P3: complete type (see vulkan_buffer.h note above)
#include "ui/game_ui.h"                 // P6: retained gameplay UI
#include "ui/mui_renderer.h"            // P3.5: MuiRenderer (Vulkan text)
#include "voxel/chunk_renderer.h"       // P3: ChunkRenderer
#include "voxel/chunk_render_system.h"  // P3: ChunkRenderSystem

#include <SDL3/SDL_scancode.h>
#include <volk.h>

#include <algorithm>
#include <chrono>
#include <filesystem>  // std::filesystem::create_directories for log dir
#include <memory>
#include <utility>

namespace snt::engine {

// ---------------------------------------------------------------------------
// FPS tracker: rolling window of frame times for averaging + plotting.
// Kept file-local (not in engine.h) since it is an implementation detail
// of Engine's frame loop.
// ---------------------------------------------------------------------------
namespace {

// Time types now come from core/clock.h (RealClock / DurationMs / TimePoint).
// The old `using Clock = std::chrono::high_resolution_clock` was replaced
// by the RealClock instance in Engine::Impl, which lets tests inject a
// ManualClock for deterministic frame timing.

struct FpsTracker {
    static constexpr int kSamples = 120;
    float frame_times_ms[kSamples] = {};
    int offset = 0;
    float last_frame_ms = 0.0f;

    void tick(float ms) {
        frame_times_ms[offset] = ms;
        offset = (offset + 1) % kSamples;
        last_frame_ms = ms;
    }

    float fps() const {
        float sum = 0.0f;
        int n = kSamples < 60 ? kSamples : 60;
        for (int i = 0; i < n; ++i) {
            int idx = (offset - 1 - i + kSamples) % kSamples;
            sum += frame_times_ms[idx];
        }
        float avg_ms = sum / n;
        return avg_ms > 0.0f ? 1000.0f / avg_ms : 0.0f;
    }
};

// ---------------------------------------------------------------------------
// TickStats: fixed-timestep logic tick tracker.
//
// Separates LOGIC (fixed-rate ticks, e.g. 20 TPS) from RENDERING (free
// frame rate). The main loop accumulates real elapsed time into an
// accumulator; each time it exceeds the fixed tick interval, one logic
// update runs. This keeps simulation deterministic regardless of render
// FPS, and lets TPS (logic) and FPS (render) be reported independently.
//
//   MSPT = milliseconds per tick (cost of the last logic update)
//   TPS  = ticks executed in the last second (clamped to tick_rate)
//
// A spiral-of-death guard caps the number of catch-up ticks per frame so
// that if the logic itself is slow, we don't pile up an unbounded debt.
// ---------------------------------------------------------------------------
struct TickStats {
    static constexpr float kTickRate    = 20.0f;             // ticks per second (target)
    static constexpr float kTickMs      = 1000.0f / kTickRate;  // 50ms per tick
    static constexpr int   kMaxCatchup  = 5;                 // max ticks per frame to avoid spiral-of-death

    float accumulator     = 0.0f;   // pending real time (ms) not yet consumed by ticks
    float last_tick_ms    = 0.0f;   // cost of the most recent logic update (MSPT)
    int   ticks_this_sec  = 0;      // ticks executed since the last TPS settle
    float second_timer    = 0.0f;   // accumulates real time; settles TPS every 1.0s
    float tps             = 0.0f;   // TPS reported for display (updated per second)

    // Consume pending time by running fixed-rate logic ticks. `tick_fn`
    // is invoked once per tick; its wall-clock cost is measured for MSPT.
    template <typename TickFn>
    void consume(float frame_ms, TickFn tick_fn) {
        accumulator += frame_ms;
        second_timer += frame_ms;

        int ticks_run = 0;
        while (accumulator >= kTickMs && ticks_run < kMaxCatchup) {
            auto t0 = std::chrono::high_resolution_clock::now();
            tick_fn();  // run one fixed-dt logic update
            auto t1 = std::chrono::high_resolution_clock::now();
            last_tick_ms = static_cast<float>(
                std::chrono::duration<double, std::milli>(t1 - t0).count());
            accumulator -= kTickMs;
            ++ticks_this_sec;
            ++ticks_run;
        }
        // If we hit the catchup cap, drop the remaining debt so a slow
        // simulation doesn't spiral — better to fall behind than to freeze.
        if (ticks_run >= kMaxCatchup) {
            accumulator = 0.0f;
        }

        // Settle TPS once per real second.
        if (second_timer >= 1000.0f) {
            tps = static_cast<float>(ticks_this_sec);
            ticks_this_sec = 0;
            second_timer -= 1000.0f;
        }
    }
};

void append_draw_data(snt::ui::UiDrawData& dst, const snt::ui::UiDrawData& src) {
    if (src.vertices.empty() || src.indices.empty()) return;
    if (src.glyph_atlas) {
        if (!dst.glyph_atlas) {
            dst.glyph_atlas = src.glyph_atlas;
        } else if (dst.glyph_atlas.get() != src.glyph_atlas.get()) {
            SNT_LOG_ERROR("MUI draw batches reference different Unicode glyph atlases; batch rejected");
            return;
        }
    }
    if (dst.vertices.size() + src.vertices.size() > 0xFFFFu) {
        SNT_LOG_WARN("UI draw data overflow while appending; dropping appended batch");
        return;
    }

    const uint16_t base = static_cast<uint16_t>(dst.vertices.size());
    dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (uint16_t index : src.indices) {
        dst.indices.push_back(static_cast<uint16_t>(base + index));
    }
}

void draw_center_crosshair(snt::ui::Arc2DCommandBuffer& commands, uint32_t width, uint32_t height) {
    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;
    constexpr float kArmLength = 8.0f;
    constexpr float kGap = 4.0f;
    constexpr float kThickness = 2.0f;

    const snt::ui::Color shadow{0, 0, 0, 160};
    const snt::ui::Color line{255, 255, 255, 220};

    auto draw_cross = [&](float offset_x, float offset_y, snt::ui::Color color) {
        commands.rect({.pos = {cx - kGap - kArmLength + offset_x,
                               cy - kThickness * 0.5f + offset_y},
                       .size = {kArmLength, kThickness}},
                      color);
        commands.rect({.pos = {cx + kGap + offset_x,
                               cy - kThickness * 0.5f + offset_y},
                       .size = {kArmLength, kThickness}},
                      color);
        commands.rect({.pos = {cx - kThickness * 0.5f + offset_x,
                               cy - kGap - kArmLength + offset_y},
                       .size = {kThickness, kArmLength}},
                      color);
        commands.rect({.pos = {cx - kThickness * 0.5f + offset_x,
                               cy + kGap + offset_y},
                       .size = {kThickness, kArmLength}},
                      color);
    };

    draw_cross(1.0f, 1.0f, shadow);
    draw_cross(0.0f, 0.0f, line);
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl: holds all subsystems. Members are pointer-free owned objects;
// order of declaration controls destruction order (reverse of init).
// ---------------------------------------------------------------------------
struct Engine::Impl {
    // Platform + input.
    snt::platform::Window window;
    snt::input::InputSystem input_system;

    // Event bus: decouples producers (Window/Engine) from consumers
    // (InputSystem/CameraSystem/future UI+script). Subscribers connect
    // during init; producers publish via enqueue + update.
    snt::ecs::EventBus event_bus;

    // Stashed config snapshot (window size, shader paths, camera defaults,
    // asset paths). Read at init; future hot-reload will overwrite + publish
    // a ConfigReloaded event.
    snt::core::EngineConfig config;

    // Engine clock. RealClock in production; tests can swap a ManualClock
    // via set_clock() before calling init() to get deterministic timing.
    snt::core::RealClock real_clock_;
    snt::core::IClock*   clock_ = &real_clock_;

    // Vulkan backend (raw stack objects — initialized in init()).
    snt::render_backend::VulkanInstance       vk_instance;
    VkSurfaceKHR                              vk_surface = VK_NULL_HANDLE;
    snt::render_backend::VulkanDevice         vk_device;
    snt::render_backend::VulkanSwapchain      vk_swapchain;
    snt::render_backend::VulkanDepth          vk_depth;
    snt::render_backend::VulkanDescriptor     vk_descriptor;
    snt::render_backend::VulkanPipeline       vk_pipeline;
    snt::render_backend::VulkanFrame          vk_frame;

    // ECS.
    snt::ecs::World world;
    // Camera entity handle — looked up by Guid (Guid=1) after load_scene.
    // Cube entities are not cached because Engine doesn't need to reference
    // them directly (RenderSystem iterates MeshRef + Transform via views).
    entt::entity camera_entity = entt::null;

    // Render system (ECS-driven). Holds raw pointers to the vk_* objects
    // above; set in init() after the vk_* objects are created.
    snt::render::RenderSystem render_system;

    // P3 Voxel rendering: chunk registry + chunk renderer + ECS render
    // system. The registry holds generated ChunkData; ChunkRenderer owns
    // the voxel GPU pipeline + buffer pool; ChunkRenderSystem drives
    // remesh (phase 1, during world.update) and draw recording (phase 2,
    // inside RenderSystem's forward pass via a registered callback).
    //
    // chunk_renderer is held via unique_ptr so its full definition (and
    // thus VertexBufferPool/VulkanBuffer) is only needed in chunk_renderer.cpp;
    // MSVC would otherwise eagerly instantiate the pool's deleter here.
    snt::data::ChunkRegistry                          chunk_registry;
    std::unique_ptr<snt::voxel::ChunkRenderer>        chunk_renderer;
    snt::voxel::ChunkRenderSystem                     chunk_render_system;
    snt::voxel::ChunkRenderSystem*                    runtime_chunk_render_system = nullptr;

    // UI renderer. Held via unique_ptr to keep
    // Vulkan types out of the Impl header; full definition is in
    // mui_renderer.cpp.
    std::unique_ptr<snt::ui::MuiRenderer>             mui_renderer;

    // P6 retained gameplay UI. Godot UI is intentionally not part of this
    // path; the controller owns engine-native hotbar/inventory/crafting
    // state and builds retained View trees each frame.
    std::unique_ptr<snt::ui::GameplayUiController>    gameplay_ui;
    snt::ui::PerformanceViewModel                     performance_ui;
    std::unique_ptr<snt::ui::UiRuntime>               gameplay_ui_runtime;
    snt::ui::Arc2DRenderer                            arc2d_renderer;
    snt::ui::UiDrawData                               ui_draw_data;

    // Per-frame state.
    FpsTracker fps_tracker;
    TickStats  tick_stats;   // fixed-timestep logic tick tracker (20 TPS)

    // P2.A2: mouse lock state (mirror of Window's relative-mouse-mode).
    // Engine toggles this based on esc_pressed / wants_mouse_lock from
    // InputState. Each frame the state is published as a MouseLockChanged
    // event; CameraSystem subscribes and skips mouse-look when unlocked.
    bool mouse_locked = false;

    // P2 Job System: real work-stealing thread pool. Installed as the
    // global default via set_default_job_system() in init() so any code
    // calling default_job_system() (ECS systems, future async asset
    // loaders) gets the multi-threaded implementation. Must be shut down
    // BEFORE Impl is destroyed so worker threads exit cleanly.
    snt::core::JobSystemP2 job_system;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() { shutdown(); }

snt::core::Expected<void> Engine::init(const snt::core::EngineConfig& config) {
    using namespace snt::platform;
    using namespace snt::render_backend;
    using namespace snt::ecs;

    SNT_LOG_INFO("Starting ScienceAndTheology engine (P2.B1)");

    // Stash config for runtime reference (future hot-reload will re-read it).
    impl_->config = config;

    // Locate engine root so all relative paths (shaders/, assets/, config/)
    // resolve independent of the process's current working directory.
    snt::core::path_utils::init();

    // --- Job System (P2) ---
    // Install the real work-stealing thread pool as the global default.
    // From this point on, default_job_system() returns impl_->job_system.
    // Worker count defaults to hardware_concurrency - 1 (>= 1).
    impl_->job_system.init();
    snt::core::set_default_job_system(&impl_->job_system);
    SNT_LOG_INFO("Job system started: %d workers",
                 impl_->job_system.worker_count());

    // --- File logger sink ---
    // Mirror all log output to logs/engine.log (relative to engine root).
    // The file is opened in append mode so restarts preserve history. If
    // the directory is missing or the file can't be created, logging
    // continues to stderr only (best-effort).
    {
        const std::string log_path =
            snt::core::path_utils::resolve("logs/engine.log");
        // Ensure the parent directory exists (std::fopen doesn't create
        // directories). create_directories is idempotent — no error if the
        // directory already exists.
        try {
            std::filesystem::create_directories(
                std::filesystem::path(log_path).parent_path());
        } catch (const std::exception& e) {
            SNT_LOG_WARN("Failed to create log directory: %s", e.what());
        }
        if (!snt::core::Logger::instance().add_file_sink(log_path.c_str())) {
            SNT_LOG_WARN("Failed to open log file '%s'; logging to stderr only",
                         log_path.c_str());
        } else {
            SNT_LOG_INFO("Logging to file: %s", log_path.c_str());
        }
    }

    // --- Window ---
    if (auto r = impl_->window.create(WindowDesc{
            .title = config.window.title,
            .width = config.window.width,
            .height = config.window.height,
            .resizable = config.window.resizable,
            .vulkan_enabled = config.window.vulkan_enabled,
        }); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (window)");
        return e;
    }
    const auto sz = impl_->window.size();
    SNT_LOG_INFO("Window created: %dx%d", sz.width, sz.height);

    // --- Input system: subscribe to SdlEventFired on the event bus ---
    // Window publishes each polled SDL event as SdlEventFired; InputSystem
    // subscribed via its on_sdl_event method. This decouples Window from
    // InputSystem — future subscribers (UI, script) can hook the same
    // events without Engine touching their wiring.
    impl_->event_bus.sink<snt::core::SdlEventFired>()
        .connect<&snt::input::InputSystem::on_sdl_event>(&impl_->input_system);

    // Window callback: enqueue + immediately flush. Immediate flush is
    // safe because InputSystem's handler is non-recursive and runs on
    // the same thread.
    impl_->window.set_event_callback(
        [this](const void* sdl_event) {
            impl_->event_bus.enqueue<snt::core::SdlEventFired>({sdl_event});
            impl_->event_bus.update();
        });

    // --- Vulkan instance ---
    if (auto r = impl_->vk_instance.init(impl_->window); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (vk_instance)");
        return e;
    }

    // --- Vulkan surface (created via platform layer) ---
    uint64_t surface_u64 = 0;
    if (auto r = impl_->window.create_vulkan_surface(
            reinterpret_cast<void*>(impl_->vk_instance.handle()), &surface_u64);
        !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (vk_surface)");
        return e;
    }
    impl_->vk_surface = reinterpret_cast<VkSurfaceKHR>(surface_u64);

    // --- Device + swapchain + depth + render pass ---
    if (auto r = impl_->vk_device.init(impl_->vk_instance.handle(), impl_->vk_surface); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (vk_device)");
        return e;
    }
    // P2.F: initialize the AssetManager now that the VulkanDevice is ready.
    // Must happen before any mesh load call; must be shut down before
    // vk_device.destroy() in shutdown().
    if (auto r = snt::assets::AssetManager::instance().init(&impl_->vk_device); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (AssetManager)");
        return e;
    }
    if (auto r = impl_->vk_swapchain.init(impl_->vk_device,
                                  static_cast<uint32_t>(sz.width),
                                  static_cast<uint32_t>(sz.height)); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (vk_swapchain)");
        return e;
    }
    if (auto r = impl_->vk_depth.init(impl_->vk_device, impl_->vk_swapchain); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (vk_depth)");
        return e;
    }

    // --- Descriptor + pipeline (dynamic rendering, no VkRenderPass) ---
    if (auto r = impl_->vk_descriptor.init(impl_->vk_device, config.render.max_entities); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (vk_descriptor)");
        return e;
    }
    if (auto r = impl_->vk_pipeline.init(impl_->vk_device, impl_->vk_descriptor,
                                 impl_->vk_swapchain.image_format(),
                                 impl_->vk_depth.format(),
                                 snt::core::path_utils::resolve(config.render.vert_shader_path),
                                 snt::core::path_utils::resolve(config.render.frag_shader_path),
                                 // MeshVertex layout: vec3 position + vec3 color.
                                 VkVertexInputBindingDescription{
                                     .binding   = 0,
                                     .stride    = sizeof(snt::render_backend::MeshVertex),
                                     .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                                 },
                                 std::vector<VkVertexInputAttributeDescription>{
                                     {.location = 0, .binding = 0,
                                      .format = VK_FORMAT_R32G32B32_SFLOAT,
                                      .offset = offsetof(snt::render_backend::MeshVertex, position)},
                                     {.location = 1, .binding = 0,
                                      .format = VK_FORMAT_R32G32B32_SFLOAT,
                                      .offset = offsetof(snt::render_backend::MeshVertex, color)},
                                 }); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (vk_pipeline)");
        return e;
    }
    // (Mesh loading moved below — needs AssetManager initialized first.)

    // --- Frame resources ---
    if (auto r = impl_->vk_frame.init(impl_->vk_device,
                              static_cast<uint32_t>(impl_->vk_swapchain.image_views().size())); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (vk_frame)");
        return e;
    }

    // --- Load scene from binary file ---
    // Scene file contains camera (Guid=1) + cube entities (Guid=2,3) with
    // their transforms + MeshRef. MeshRef paths are resolved via the
    // AssetManager's mesh cache (already initialized above).
    //
    // Replaces the previous hardcoded entity creation (P2.B1): instead
    // of building entities in C++ code, the scene is data-driven — edit
    // scenes/default_scene.bin with gen_default_scene to change content.
    const std::string scene_path =
        snt::core::path_utils::resolve(config.scene.path);
    auto scene_result = snt::scene::load_scene(
        impl_->world,
        snt::assets::AssetManager::instance().mesh_cache(),
        scene_path);
    if (!scene_result) {
        snt::core::Error e = scene_result.error();
        e.with_context("Engine::init (load_scene)");
        return e;
    }

    // Look up the camera entity by its stable Guid (Guid=1, defined by
    // gen_default_scene). This decouples Engine from entity creation order.
    impl_->camera_entity = impl_->world.find_entity_by_guid(snt::ecs::EntityGuid{1});
    if (impl_->camera_entity == entt::null) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Engine::init: scene has no camera entity (Guid=1)"};
    }

    // P3: remove the two demo cube entities (Guid=2, Guid=3) from the scene.
    // They intersect with the voxel ground and are no longer needed for
    // P3 verification — the chunk renderer is the new visual output.
    for (uint64_t g : {2ull, 3ull}) {
        entt::entity e = impl_->world.find_entity_by_guid(snt::ecs::EntityGuid{g});
        if (e != entt::null) {
            impl_->world.destroy_entity(e);
        }
    }

    // Update camera aspect to match the actual window size (the scene file
    // stores a default aspect that may differ from the runtime window).
    if (impl_->world.registry().all_of<snt::ecs::Camera>(impl_->camera_entity)) {
        auto& cam = impl_->world.registry().get<snt::ecs::Camera>(impl_->camera_entity);
        cam.aspect = static_cast<float>(sz.width) / static_cast<float>(sz.height);
    }
    // P3: raise the camera above the ground so the y=0 surface is visible
    // when looking down. Scene default is [0,0,3] (same height as ground).
    if (impl_->world.registry().all_of<snt::ecs::Transform>(impl_->camera_entity)) {
        auto& cam_t = impl_->world.registry().get<snt::ecs::Transform>(impl_->camera_entity);
        cam_t.position[0] = 4.0f;
        cam_t.position[1] = 4.0f;
        cam_t.position[2] = 8.0f;
    }

    // --- Render system (P2.D) ---
    // ECS-driven rendering via RenderGraph. RenderSystem owns its own
    // RenderGraph instance; Engine wires up the backend dependencies.
    impl_->render_system.set_device(&impl_->vk_device);
    impl_->render_system.set_swapchain(&impl_->vk_swapchain);
    impl_->render_system.set_depth(&impl_->vk_depth);
    impl_->render_system.set_pipeline(&impl_->vk_pipeline);
    impl_->render_system.set_descriptor(&impl_->vk_descriptor);
    impl_->render_system.set_frame(&impl_->vk_frame);
    impl_->render_system.set_active_camera(impl_->camera_entity);
    if (auto r = impl_->render_system.init_render_graph(); !r) {
        snt::core::Error e = r.error();
        e.with_context("Engine::init (init_render_graph)");
        return e;
    }

    // --- P3: Voxel chunk rendering setup ---
    // One-shot wiring: init the ChunkRenderer, wire the pure-data
    // ChunkRenderSystem, run the optional demo bootstrap, then register a
    // real RenderGraph pass provider for chunk draws.
    {
        const uint32_t max_chunks = config.voxel.max_chunks;
        impl_->chunk_renderer = std::make_unique<snt::voxel::ChunkRenderer>();
        if (auto r = impl_->chunk_renderer->init(
                impl_->vk_device,
                impl_->vk_swapchain.image_format(),
                impl_->vk_depth.format(),
                snt::core::path_utils::resolve("shaders/voxel.vert.spv"),
                snt::core::path_utils::resolve("shaders/voxel.frag.spv"),
                max_chunks); !r) {
            snt::core::Error e = r.error();
            e.with_context("Engine::init (chunk_renderer)");
            return e;
        }

        // Wire ChunkRenderSystem and register it with the World so its
        // update() runs each frame (phase 1: schedule/upload dirty chunks).
        // add_system move-constructs a new instance from impl_'s (which has
        // renderer_/registry_ pointers already wired) and returns a stable
        // reference to the heap-owned instance.
        impl_->chunk_render_system.set_chunk_renderer(impl_->chunk_renderer.get());
        impl_->chunk_render_system.set_chunk_registry(&impl_->chunk_registry);
        impl_->chunk_render_system.set_remesh_jobs_per_frame(
            config.voxel.remesh_jobs_per_frame);
        impl_->chunk_render_system.set_uploads_per_frame(
            config.voxel.uploads_per_frame);
        if (auto r = bootstrap_demo_world(
                DemoWorldBootstrapDesc{
                    .enabled = config.demo.bootstrap_chunks,
                    .seed = config.demo.seed,
                },
                impl_->chunk_registry,
                impl_->chunk_render_system); !r) {
            snt::core::Error e = r.error();
            e.with_context("Engine::init (bootstrap_demo_world)");
            return e;
        }
        auto& chunk_sys = impl_->world.add_system<snt::voxel::ChunkRenderSystem>(
            std::move(impl_->chunk_render_system));
        impl_->runtime_chunk_render_system = &chunk_sys;

        // --- P4: first-person player controller ---
        // The controller owns movement, gravity, voxel collision, camera
        // transform sync, and left-click block breaking. This replaces the
        // old fly-camera movement path while keeping the camera entity as the
        // render system's active view.
        auto& player_controller =
            impl_->world.add_system<snt::player::PlayerControllerSystem>();
        player_controller.set_input(&impl_->input_system);
        player_controller.set_chunk_registry(&impl_->chunk_registry);
        player_controller.set_chunk_render_system(impl_->runtime_chunk_render_system);
        player_controller.set_camera_entity(impl_->camera_entity);
        player_controller.set_dimension_id("overworld");
        player_controller.set_spawn_feet_position({4.0f, 6.0f, 8.0f});
        player_controller.set_initial_look(-90.0f, -25.0f);
        snt::player::PlayerControllerTuning player_tuning;
        player_tuning.move_speed = config.camera.move_speed > 0.0f
            ? config.camera.move_speed
            : player_tuning.move_speed;
        player_tuning.look_speed = config.camera.look_speed;
        player_controller.set_tuning(player_tuning);
        impl_->event_bus.sink<snt::core::MouseLockChanged>()
            .connect<&snt::player::PlayerControllerSystem::on_mouse_lock_changed>(
                &player_controller);

        // Register voxel terrain as its own RenderGraph pass.
        auto* chunk_sys_ptr = &chunk_sys;
        impl_->render_system.add_pass_provider(
            [chunk_sys_ptr](snt::render::RenderPassBuildContext& ctx) {
                auto* pass = ctx.graph.add_pass("voxel_chunks");
                if (!pass) {
                    SNT_LOG_ERROR("Failed to add voxel_chunks render pass");
                    return;
                }
                if (!ctx.last_color_pass.empty()) {
                    pass->depends_on.push_back(ctx.last_color_pass);
                }
                pass->color_attachments.push_back(snt::renderer::ColorAttachmentDecl{
                    .resource = ctx.color_resource,
                    .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                });
                pass->depth_attachment = snt::renderer::DepthAttachmentDecl{
                    .resource = ctx.depth_resource,
                    .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                };

                const auto view = ctx.view;
                const auto proj = ctx.proj;
                const auto extent = ctx.extent;
                const uint32_t frame_idx = ctx.frame_idx;
                pass->execute = [chunk_sys_ptr, view, proj, extent, frame_idx]
                                (snt::render_backend::CommandContext& pass_ctx) {
                    VkCommandBuffer cmd = pass_ctx.handle();
                    VkViewport viewport{
                        .x = 0.0f,
                        .y = 0.0f,
                        .width = static_cast<float>(extent.width),
                        .height = static_cast<float>(extent.height),
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f,
                    };
                    vkCmdSetViewport(cmd, 0, 1, &viewport);
                    VkRect2D scissor{.offset = {0, 0}, .extent = extent};
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    chunk_sys_ptr->render(cmd, frame_idx, view.data(), proj.data());
                };
                ctx.last_color_pass = pass->name;
            });
    }

    // --- P6: retained gameplay UI controller ---
    // Engine-native replacement path for gameplay UI. It starts with a
    // small demo inventory/recipe set so hotbar/crafting can render before
    // full ECS inventory plumbing lands.
    impl_->gameplay_ui = std::make_unique<snt::ui::GameplayUiController>(
        snt::ui::InventoryViewModel{snt::ui::make_p6_demo_inventory()},
        snt::ui::make_p6_demo_recipes());
    snt::ui::TextEngineConfig text_config;
    text_config.font_paths = config.ui.font_paths;
    text_config.locale = config.ui.locale;
    text_config.icu_data_path = config.ui.icu_data_path;
    impl_->gameplay_ui_runtime = std::make_unique<snt::ui::UiRuntime>(std::move(text_config));
    if (!impl_->gameplay_ui_runtime->text_available()) {
        SNT_LOG_ERROR("P6 Unicode MUI text initialization failed: %s",
                      impl_->gameplay_ui_runtime->text_initialization_error().c_str());
    }
    SNT_LOG_INFO("P6 retained gameplay UI initialized (Godot UI path not used)");

    // --- P6: retained MUI renderer ---
    // The retained MUI renderer is always active. Text glyphs arrive through
    // the Unicode glyph-atlas contract rather than a static font path.
    {
        impl_->mui_renderer = std::make_unique<snt::ui::MuiRenderer>();
            VkFormat color_format = impl_->vk_swapchain.image_format();
        auto r = impl_->mui_renderer->init(impl_->vk_device, color_format);
            if (!r) {
                SNT_LOG_ERROR("MuiRenderer init failed: %s",
                              r.error().format().c_str());
                impl_->mui_renderer.reset();
            } else {
                // Register UI as its own RenderGraph pass. It only writes
                // color and uses LOAD so it composites over the previous pass.
                auto* mui_renderer_ptr = impl_->mui_renderer.get();
                auto* ui_draw_data_ptr = &impl_->ui_draw_data;
                impl_->render_system.add_pass_provider(
                    [mui_renderer_ptr, ui_draw_data_ptr](snt::render::RenderPassBuildContext& ctx) {
                        auto* pass = ctx.graph.add_pass("ui_overlay");
                        if (!pass) {
                            SNT_LOG_ERROR("Failed to add ui_overlay render pass");
                            return;
                        }
                        if (!ctx.last_color_pass.empty()) {
                            pass->depends_on.push_back(ctx.last_color_pass);
                        }
                        pass->color_attachments.push_back(snt::renderer::ColorAttachmentDecl{
                            .resource = ctx.color_resource,
                            .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                            .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                        });

                        const auto extent = ctx.extent;
                        pass->execute = [mui_renderer_ptr, ui_draw_data_ptr, extent]
                                        (snt::render_backend::CommandContext& pass_ctx) {
                            VkCommandBuffer cmd = pass_ctx.handle();
                            VkViewport viewport{
                                .x = 0.0f,
                                .y = 0.0f,
                                .width = static_cast<float>(extent.width),
                                .height = static_cast<float>(extent.height),
                                .minDepth = 0.0f,
                                .maxDepth = 1.0f,
                            };
                            vkCmdSetViewport(cmd, 0, 1, &viewport);
                            VkRect2D scissor{.offset = {0, 0}, .extent = extent};
                            vkCmdSetScissor(cmd, 0, 1, &scissor);
                            mui_renderer_ptr->render(cmd, *ui_draw_data_ptr);
                        };
                        ctx.last_color_pass = pass->name;
                    });
        }
    }

    SNT_LOG_INFO("Controls (MC-style):\n"
                 "  WASD       = move (A/D strafe)\n"
                 "  Space      = ascend\n"
                 "  LShift     = descend\n"
                 "  Double-W   = sprint (2x while W held)\n"
                 "  Mouse      = free-look (auto-locked)\n"
                 "  ESC        = release mouse\n"
                 "  Click      = re-lock mouse\n"
                 "  E          = P6 inventory UI\n"
                 "  C          = P6 crafting UI\n"
                 "  Enter      = craft first available recipe while crafting UI is open\n"
                 "  F3         = P6 retained performance panel");

    // P2.A2: enter relative mouse mode so the user starts in free-look.
    // ESC releases the mouse; clicking the window re-locks it.
    if (auto r = impl_->window.set_relative_mouse_mode(true); r) {
        impl_->mouse_locked = true;
    } else {
        // Non-fatal: engine still runs with the OS cursor visible.
        SNT_LOG_WARN("Failed to enter relative mouse mode: %s",
                     r.error().format().c_str());
        impl_->mouse_locked = false;
    }

    // Memory snapshot after init: establishes a baseline so the shutdown
    // dump can highlight leaks (anything still allocated above this point
    // that the engine doesn't explicitly own).
    SNT_LOG_INFO("Memory stats after init:");
    snt::core::MemoryTracker::instance().log_stats();
    return {};
}

void Engine::run() {
    using namespace snt::render_backend;

    auto last_time = impl_->clock_->now();
    while (impl_->window.poll_events()) {
        SNT_PROFILE_SCOPE("frame");  // Per-frame zone (no-op until a backend is wired in)
        auto now = impl_->clock_->now();
        float frame_ms = impl_->clock_->delta_since(last_time).count();
        float dt = frame_ms / 1000.0f;
        last_time = now;
        impl_->fps_tracker.tick(frame_ms);

        // Finalize input for this frame.
        impl_->input_system.end_frame();
        const auto& input_state = impl_->input_system.state();

        if (impl_->gameplay_ui) {
            if (input_state.key_pressed[SDL_SCANCODE_E]) {
                impl_->gameplay_ui->toggle_inventory();
                SNT_LOG_INFO("P6 inventory UI %s",
                             impl_->gameplay_ui->inventory_open() ? "opened" : "closed");
            }
            if (input_state.key_pressed[SDL_SCANCODE_C]) {
                impl_->gameplay_ui->toggle_crafting();
                SNT_LOG_INFO("P6 crafting UI %s",
                             impl_->gameplay_ui->crafting_open() ? "opened" : "closed");
            }
            if (input_state.key_pressed[SDL_SCANCODE_RETURN] &&
                impl_->gameplay_ui->crafting_open()) {
                for (const auto& recipe : impl_->gameplay_ui->crafting().recipes()) {
                    if (!impl_->gameplay_ui->crafting().can_craft(recipe)) continue;
                    auto result = impl_->gameplay_ui->crafting().craft(recipe.id);
                    if (result.ok) {
                        SNT_LOG_INFO("P6 crafted %s x%d",
                                     result.output.item_key.c_str(),
                                     result.output.count);
                    } else {
                        SNT_LOG_WARN("P6 craft failed: %s", result.reason.c_str());
                    }
                    break;
                }
            }
        }
        if (input_state.key_pressed[SDL_SCANCODE_F3]) {
            impl_->performance_ui.toggle_visible();
            SNT_LOG_INFO("P6 performance UI %s",
                         impl_->performance_ui.visible() ? "opened" : "closed");
        }

        // --- P2.A2: mouse lock management (MC-style) ---
        // ESC releases the pointer; a left click (when unlocked) re-locks.
        // The lock state is forwarded to CameraSystem so it knows whether
        // to apply mouse-look this frame.
        bool gameplay_ui_open =
            impl_->gameplay_ui &&
            (impl_->gameplay_ui->inventory_open() || impl_->gameplay_ui->crafting_open());
        if (input_state.esc_pressed && gameplay_ui_open) {
            impl_->gameplay_ui->close();
            gameplay_ui_open = false;
            SNT_LOG_INFO("P6 gameplay UI closed");
        }
        if ((input_state.esc_pressed || gameplay_ui_open) && impl_->mouse_locked) {
            // Lock-state toggle failures are non-fatal; ignore but keep
            // the local `mouse_locked` flag in sync with intent.
            auto _ = impl_->window.set_relative_mouse_mode(false);
            (void)_;
            impl_->mouse_locked = false;
        } else if (input_state.wants_mouse_lock && !impl_->mouse_locked && !gameplay_ui_open) {
            auto _ = impl_->window.set_relative_mouse_mode(true);
            (void)_;
            impl_->mouse_locked = true;
        }

        // Forward lock state via the event bus (replaces direct call to
        // camera_system->set_mouse_locked). Published every frame so
        // CameraSystem + future subscribers (UI cursor, etc.) stay in
        // sync. The event is enqueued + flushed immediately.
        impl_->event_bus.enqueue<snt::core::MouseLockChanged>({impl_->mouse_locked});
        impl_->event_bus.update();

        // Fixed-timestep logic ticks (20 TPS), decoupled from render FPS.
        // The accumulator consumes real elapsed time in kTickMs chunks;
        // each chunk triggers one world.update with a FIXED dt so the
        // simulation is deterministic regardless of render rate. A
        // spiral-of-death guard (kMaxCatchup) prevents unbounded catch-up
        // if the logic itself runs slower than real time.
        constexpr float kFixedDt = TickStats::kTickMs / 1000.0f;  // 0.05s
        impl_->tick_stats.consume(frame_ms, [&]() {
            impl_->world.update(kFixedDt);
        });
        impl_->input_system.new_frame();

        impl_->performance_ui.publish({
            .fps = impl_->fps_tracker.fps(),
            .frame_ms = impl_->fps_tracker.last_frame_ms,
            .tps = impl_->tick_stats.tps,
            .mspt = impl_->tick_stats.last_tick_ms,
            .job_workers = static_cast<int32_t>(
                snt::core::default_job_system().worker_count()),
        });

        impl_->ui_draw_data = {};
        const auto& extent = impl_->vk_swapchain.extent();
        if (impl_->gameplay_ui) {
            auto root = snt::ui::build_gameplay_ui_root(
                *impl_->gameplay_ui,
                {static_cast<float>(extent.width), static_cast<float>(extent.height)});
            auto frame = impl_->gameplay_ui_runtime->build_frame(
                *root,
                {static_cast<float>(extent.width), static_cast<float>(extent.height)});
            impl_->ui_draw_data = std::move(frame.draw_data);
        }
        if (impl_->performance_ui.visible()) {
            auto panel = snt::ui::build_performance_panel_view(impl_->performance_ui);
            auto frame = impl_->gameplay_ui_runtime->build_frame(
                *panel,
                {static_cast<float>(extent.width), static_cast<float>(extent.height)});
            append_draw_data(impl_->ui_draw_data, frame.draw_data);
        }
        if (impl_->mouse_locked) {
            snt::ui::Arc2DCommandBuffer crosshair_commands;
            draw_center_crosshair(crosshair_commands, extent.width, extent.height);
            append_draw_data(impl_->ui_draw_data,
                             impl_->arc2d_renderer.build_draw_data(crosshair_commands));
        }

        // Update the UI orthographic projection for the current swapchain
        // extent (pixel-space → clip-space). Must happen before
        // render_system.update() so the UBO is current when UI draws.
        if (impl_->mui_renderer) {
            if (auto sync = impl_->mui_renderer->synchronize_glyph_atlas(impl_->ui_draw_data); !sync) {
                SNT_LOG_ERROR("MUI glyph atlas synchronization failed: %s",
                              sync.error().format().c_str());
            }
            const auto& extent = impl_->vk_swapchain.extent();
            impl_->mui_renderer->update_ortho(extent.width, extent.height);
        }

        // Render: RenderSystem reads ECS state and draws via VulkanFrame.
        impl_->render_system.update(impl_->world, dt);

        // Handle resize: RenderSystem sets needs_resize_ when draw_frame
        // signals swapchain-out-of-date.
        if (impl_->render_system.needs_resize()) {
            impl_->vk_device.wait_idle();
            const auto new_sz = impl_->window.size();
            if (impl_->vk_swapchain.recreate(static_cast<uint32_t>(new_sz.width),
                                             static_cast<uint32_t>(new_sz.height))) {
                impl_->vk_depth.recreate(impl_->vk_swapchain);
                auto& cam_comp_resize = impl_->world.get_component<snt::ecs::Camera>(impl_->camera_entity);
                cam_comp_resize.aspect = static_cast<float>(new_sz.width) /
                                         static_cast<float>(new_sz.height);
                SNT_LOG_INFO("Swapchain recreated: %dx%d",
                             new_sz.width, new_sz.height);
            }
        }
    }

    // Drain GPU before teardown.
    impl_->vk_device.wait_idle();
}

void Engine::shutdown() {
    if (!impl_) return;
    // vk_device.wait_idle() already called at end of run(); call again
    // in case shutdown() is invoked without run() returning normally.
    impl_->vk_device.wait_idle();

    // Disconnect all event bus subscribers before subsystems are destroyed.
    // Without this, a late event publish could invoke a dangling method on
    // a destroyed InputSystem / CameraSystem.
    impl_->event_bus.clear();

    // Render system (releases its RenderGraph).
    impl_->render_system.destroy_render_graph();

    // P3: ChunkRenderer (voxel pipeline + descriptor + buffer pool). Must
    // be destroyed before the VulkanDevice since its GPU resources are
    // backed by VMA allocations tied to the device.
    if (impl_->chunk_renderer) {
        impl_->chunk_renderer->destroy();
        impl_->chunk_renderer.reset();
    }

    // P3.5: MuiRenderer (font atlas texture + UI pipeline + descriptor).
    // Same ordering constraint as ChunkRenderer.
    if (impl_->mui_renderer) {
        impl_->mui_renderer->destroy();
        impl_->mui_renderer.reset();
    }

    // Frame + pipeline + descriptor.
    // (AssetManager-owned meshes are destroyed by AssetManager::shutdown() below.)
    impl_->vk_frame.destroy();
    impl_->vk_pipeline.destroy();
    impl_->vk_descriptor.destroy();

    // Depth + swapchain.
    impl_->vk_depth.destroy();
    impl_->vk_swapchain.destroy();

    // AssetManager: release all cached meshes BEFORE the VulkanDevice is
    // destroyed. Meshes hold VMA allocations backed by the device; if they
    // outlive vk_device.destroy(), VMA hits an "allocations not freed"
    // assertion (observed during P4 scene-loading work).
    //
    // Must come AFTER vk_frame/pipeline/descriptor/swapchain destroy()
    // because those may still reference mesh data during teardown.
    snt::assets::AssetManager::instance().shutdown();

    // Device + surface + instance.
    impl_->vk_device.destroy();
    if (impl_->vk_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(impl_->vk_instance.handle(), impl_->vk_surface, nullptr);
        impl_->vk_surface = VK_NULL_HANDLE;
    }
    impl_->vk_instance.destroy();

    // Window.
    impl_->window.destroy();

    // Job System: stop worker threads. Must happen AFTER all subsystems
    // that might submit jobs (RenderSystem, AssetManager) have shut down,
    // so no new work arrives during drain. Graceful shutdown waits for
    // any in-flight jobs to finish before joining worker threads.
    snt::core::set_default_job_system(nullptr);
    impl_->job_system.shutdown();

    SNT_LOG_INFO("Shutdown complete");

    // Release Impl so a second shutdown() call (e.g. from the destructor
    // after main() already called shutdown()) is a no-op via the
    // `if (!impl_) return` guard above.
    impl_.reset();

    // Final memory snapshot. After Impl is destroyed, anything still
    // counted is either a global/static allocation (expected) or a leak
    // (investigate). Compare against the init() baseline logged above.
    SNT_LOG_INFO("Memory stats after shutdown:");
    snt::core::MemoryTracker::instance().log_stats();
}

// ---------------------------------------------------------------------------
// Time accessors.
// ---------------------------------------------------------------------------
// Forwarding methods to Impl::clock_. The Impl already owns a RealClock
// (real_clock_) + a swappable pointer (clock_); these methods just expose
// that to external callers without breaking PImpl encapsulation.
//
// set_clock(nullptr) restores the default RealClock so callers don't have
// to remember the original clock when restoring default behavior.
snt::core::IClock& Engine::get_clock() {
    return *impl_->clock_;
}

snt::core::TimePoint Engine::get_time() {
    return impl_->clock_->now();
}

void Engine::set_clock(snt::core::IClock* clock) {
    // Null restores the default RealClock. Non-null installs the caller-
    // owned clock (caller keeps ownership + lifetime responsibility).
    impl_->clock_ = (clock != nullptr) ? clock : &impl_->real_clock_;
}

}  // namespace snt::engine
