// Engine: top-level orchestrator for the ScienceAndTheology engine.
//
// Owns the window, Vulkan device + swapchain + render pass + pipeline +
// frame resources, the ECS World, and the InputSystem. Exposes init/run/
// shutdown so main.cpp is reduced to:
//
//   Engine engine;
//   if (!engine.init()) return 1;
//   engine.run();
//   engine.shutdown();
//
// P2.B1 scope: wraps the existing P1.5 logic 1:1. The per-frame loop still
// calls VulkanFrame::draw_frame directly (the old rendering path).
// P2.C will introduce RenderSystem and route rendering through the ECS.
// P2.D will swap draw_frame for RenderGraph.

#pragma once

#include <memory>
#include <string_view>

#include "core/clock.h"         // IClock, TimePoint (time accessors)
#include "core/engine_config.h"  // EngineConfig for init
#include "core/expected.h"       // Expected<void> for init
#include "core/path_utils.h"    // RuntimePaths for host-owned resource roots

namespace snt::platform { class Window; }
namespace snt::input   { class InputSystem; }
namespace snt::ecs     { class World; }
namespace snt::render_backend {
    class VulkanInstance;
    class VulkanDevice;
    class VulkanSwapchain;
    class VulkanDepth;
    class VulkanDescriptor;
    class VulkanPipeline;
    class VulkanMesh;
    class VulkanFrame;
}

namespace snt::engine {

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // One-time initialization. The host supplies its package layout rather
    // than the engine inferring a repository root or submodule directory.
    snt::core::Expected<void> init(const snt::core::EngineConfig& config,
                                   snt::core::RuntimePaths runtime_paths);

    // Main loop. Returns when the window requests close.
    void run();

    // Release all resources. Idempotent.
    void shutdown();

    // Engine command boundary for local console, editor, and server-admin
    // frontends. P7.1 currently owns `/snt reload`; unsupported commands are
    // rejected explicitly instead of reaching subsystems directly.
    snt::core::Expected<void> execute_command(std::string_view command);

    // -----------------------------------------------------------------------
    // Time accessors.
    // -----------------------------------------------------------------------
    // Expose the engine clock so subsystems (animation, physics, scripting,
    // interpolation) read time through a single interface instead of
    // calling std::chrono directly. This lets tests inject a ManualClock
    // via set_clock() for deterministic frame timing, and leaves room for
    // future time scaling (pause / slow-mo / fast-forward) without touching
    // call sites.
    //
    // Lifecycle:
    //   - Default clock is a RealClock backed by steady_clock.
    //   - set_clock(custom) BEFORE init() makes the engine run on the
    //     injected clock; the caller owns it and must keep it alive until
    //     shutdown() or the next set_clock() call.
    //   - set_clock(nullptr) restores the default RealClock.

    // Access the engine clock. Systems that need time should read from
    // here, not from std::chrono directly.
    snt::core::IClock& get_clock();

    // Convenience: current engine time point (== get_clock().now()).
    snt::core::TimePoint get_time();

    // Test injection: swap in a ManualClock before init() for deterministic
    // timing. Pass nullptr to restore the default RealClock. Non-owning;
    // caller is responsible for the lifetime of `clock`.
    void set_clock(snt::core::IClock* clock);

private:
    // PImpl to keep Vulkan / SDL types out of the header.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::engine
