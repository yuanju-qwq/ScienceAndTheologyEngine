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

#include "core/engine_config.h"  // EngineConfig for init
#include "core/expected.h"       // Expected<void> for init

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

    // One-time initialization. `config` supplies all tunable parameters
    // (window size, shader paths, camera defaults, asset paths). Pass a
    // default-constructed EngineConfig to use built-in defaults.
    snt::core::Expected<void> init(const snt::core::EngineConfig& config);

    // Convenience overload: uses default-constructed EngineConfig.
    snt::core::Expected<void> init() {
        return init(snt::core::EngineConfig{});
    }

    // Main loop. Returns when the window requests close.
    void run();

    // Release all resources. Idempotent.
    void shutdown();

private:
    // PImpl to keep Vulkan / SDL types out of the header.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::engine
