// ClientRuntime: SDL/Vulkan presentation host layered over SimulationRuntime.
//
// This target owns window/input, GPU asset residency, rendering, voxel mesh
// uploads, UI, and client callbacks. It composes the same SimulationRuntime
// used by a server but never lets those dependencies cross into that target.

#pragma once

#include "core/expected.h"
#include "core/path_utils.h"
#include "core/runtime_config.h"
#include "engine/simulation_runtime.h"

#include <memory>

namespace snt::ecs {
struct EntityGuid;
}
namespace snt::ui {
class Arc2DCommandBuffer;
class View;
}

namespace snt::engine {

class IClientSession;

class ClientRuntime {
public:
    ClientRuntime();
    ~ClientRuntime();

    ClientRuntime(const ClientRuntime&) = delete;
    ClientRuntime& operator=(const ClientRuntime&) = delete;

    snt::core::Expected<void> init(const snt::core::RuntimeConfig& config,
                                   snt::core::RuntimePaths runtime_paths,
                                   std::unique_ptr<IClientSession> session);
    void run();
    void shutdown();

    void request_stop() noexcept;
    [[nodiscard]] SimulationRuntime& simulation() noexcept { return simulation_; }

private:
    friend class ClientFrameContext;
    friend class ClientUiContext;
    friend class ClientWorldSession;

    struct Impl;

    bool mouse_locked() const noexcept;
    snt::core::Expected<void> set_mouse_locked(bool locked);
    snt::core::Expected<void> set_active_camera(snt::ecs::EntityGuid guid);
    void submit_ui(snt::ui::View& root);
    void submit_ui(const snt::ui::Arc2DCommandBuffer& commands);

    SimulationRuntime simulation_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::engine
