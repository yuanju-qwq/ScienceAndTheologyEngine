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
#include "data/defs/chunk_data.h"       // P3: ChunkData::kChunkSize
#include "data/defs/world_seed.h"       // P3: WorldSeed for TerrainGenerator
#include "data/world/chunk_registry.h"  // P3: ChunkRegistry
#include "data/world_gen/terrain_generator.h"  // P3: generate test chunk
#include "ecs/camera_system.h"
#include "ecs/components.h"
#include "ecs/entity_guid.h"
#include "ecs/event_bus.h"        // EventBus = entt::dispatcher
#include "ecs/world.h"
#include "input/input_system.h"
#include "platform/window.h"
#include "render/render_system.h"
#include "scene/scene.h"          // load_scene for binary scene loading
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
#include "ui/debug_panel.h"
#include "ui/mui.h"
#include "voxel/chunk_renderer.h"       // P3: ChunkRenderer
#include "voxel/chunk_render_system.h"  // P3: ChunkRenderSystem

#include <volk.h>

#include <filesystem>  // std::filesystem::create_directories for log dir

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

    // Per-frame state.
    FpsTracker fps_tracker;

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

    // Update camera aspect to match the actual window size (the scene file
    // stores a default aspect that may differ from the runtime window).
    if (impl_->world.registry().all_of<snt::ecs::Camera>(impl_->camera_entity)) {
        auto& cam = impl_->world.registry().get<snt::ecs::Camera>(impl_->camera_entity);
        cam.aspect = static_cast<float>(sz.width) / static_cast<float>(sz.height);
    }

    // --- Camera system ---
    auto& camera_system = impl_->world.add_system<CameraSystem>();
    camera_system.set_input(&impl_->input_system);
    camera_system.set_active_camera(impl_->camera_entity);
    camera_system.set_move_speed(config.camera.move_speed);
    camera_system.set_look_speed(config.camera.look_speed);

    // CameraSystem subscribes to MouseLockChanged on the event bus. Engine
    // publishes the lock state each frame instead of calling
    // set_mouse_locked() directly — future modules (e.g. UI cursor
    // visibility) can subscribe to the same event.
    impl_->event_bus.sink<snt::core::MouseLockChanged>()
        .connect<&snt::ecs::CameraSystem::on_mouse_lock_changed>(&camera_system);

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
    // One-shot wiring: init the ChunkRenderer (voxel pipeline + descriptor
    // + buffer pool), generate a test chunk at the origin via
    // TerrainGenerator, store it in ChunkRegistry, create a ChunkRenderRef
    // entity so ChunkRenderSystem remeshes + draws it, and register a
    // forward-pass callback so chunk draws land inside RenderSystem's pass.
    {
        constexpr uint32_t kMaxChunks = 64;
        impl_->chunk_renderer = std::make_unique<snt::voxel::ChunkRenderer>();
        if (auto r = impl_->chunk_renderer->init(
                impl_->vk_device,
                impl_->vk_swapchain.image_format(),
                impl_->vk_depth.format(),
                snt::core::path_utils::resolve("shaders/voxel.vert.spv"),
                snt::core::path_utils::resolve("shaders/voxel.frag.spv"),
                kMaxChunks); !r) {
            snt::core::Error e = r.error();
            e.with_context("Engine::init (chunk_renderer)");
            return e;
        }

        // Generate a test chunk at the origin so there's something to see.
        // Flat-world TerrainGenerator with a fixed seed; no config snapshot
        // (uses built-in defaults). The chunk spans world [0,32)^3.
        snt::data::TerrainGenerator terrain_gen(snt::data::WorldSeed(20240601u));
        snt::data::ChunkData chunk = terrain_gen.generate_chunk("overworld", 0, 0, 0);
        impl_->chunk_registry.set_chunk("overworld", 0, 0, 0, std::move(chunk));
        SNT_LOG_INFO("P3: generated test chunk at (0,0,0), %zu terrain cells",
                     impl_->chunk_registry.get_chunk("overworld", 0, 0, 0)
                         ->terrain.cells.size());

        // Create the ChunkRenderRef entity. dirty=true so ChunkRenderSystem
        // remeshes it on the first update().
        auto chunk_e = impl_->world.registry().create();
        impl_->world.registry().emplace<snt::ecs::ChunkRenderRef>(
            chunk_e, 0, 0, 0,
            snt::ecs::ChunkRenderRef{}.mesh_handle_id, /*dirty=*/true);

        // Wire ChunkRenderSystem and register it with the World so its
        // update() runs each frame (phase 1: remesh dirty chunks).
        // add_system move-constructs a new instance from impl_'s (which has
        // renderer_/registry_ pointers already wired) and returns a stable
        // reference to the heap-owned instance.
        impl_->chunk_render_system.set_chunk_renderer(impl_->chunk_renderer.get());
        impl_->chunk_render_system.set_chunk_registry(&impl_->chunk_registry);
        auto& chunk_sys = impl_->world.add_system<snt::voxel::ChunkRenderSystem>(
            std::move(impl_->chunk_render_system));

        // Register the forward-pass callback (phase 2: record chunk draws
        // inside RenderSystem's pass). Captures the live system pointer +
        // the world so it can iterate ChunkRenderRef entities.
        auto* chunk_sys_ptr = &chunk_sys;
        snt::ecs::World* world_ptr = &impl_->world;
        impl_->render_system.set_forward_pass_callback(
            [chunk_sys_ptr, world_ptr](VkCommandBuffer cmd, uint32_t frame_idx,
                                        const float view[16], const float proj[16]) {
                chunk_sys_ptr->render(cmd, frame_idx, view, proj, *world_ptr);
            });
    }

    // --- Debug panel metrics ---
    auto& panel = snt::ui::default_debug_panel();
    panel.register_metric("FPS",
                          [this]() { return impl_->fps_tracker.fps(); });
    panel.register_metric("Frame Time (ms)",
                          [this]() { return impl_->fps_tracker.last_frame_ms; });
    panel.register_metric("Job Workers", []() {
        return static_cast<float>(snt::core::default_job_system().worker_count());
    });

    SNT_LOG_INFO("Controls (MC-style):\n"
                 "  WASD       = move (A/D strafe)\n"
                 "  Space      = ascend\n"
                 "  LShift     = descend\n"
                 "  Double-W   = sprint (2x while W held)\n"
                 "  Mouse      = free-look (auto-locked)\n"
                 "  ESC        = release mouse\n"
                 "  Click      = re-lock mouse");

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

        // --- P2.A2: mouse lock management (MC-style) ---
        // ESC releases the pointer; a left click (when unlocked) re-locks.
        // The lock state is forwarded to CameraSystem so it knows whether
        // to apply mouse-look this frame.
        if (input_state.esc_pressed && impl_->mouse_locked) {
            // Lock-state toggle failures are non-fatal; ignore but keep
            // the local `mouse_locked` flag in sync with intent.
            auto _ = impl_->window.set_relative_mouse_mode(false);
            (void)_;
            impl_->mouse_locked = false;
        } else if (input_state.wants_mouse_lock && !impl_->mouse_locked) {
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

        // Let ECS read input + run systems.
        impl_->world.update(dt);
        impl_->input_system.new_frame();

        // Sample metrics + draw debug panel (P1 stub: no visible output).
        auto& panel = snt::ui::default_debug_panel();
        panel.sample();
        snt::ui::MuiContext& mui = snt::ui::default_mui_context();
        mui.begin_frame();
        panel.draw();
        mui.end_frame();

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
