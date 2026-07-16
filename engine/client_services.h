// ClientRuntime presentation callback contracts.
//
// These types are intentionally separate from simulation_services.h. They
// may expose local input, GPU mesh residency, chunk rendering, camera state,
// and UI submission because only ClientRuntime can construct them.

#pragma once

#include "core/expected.h"
#include "ecs/entity_guid.h"
#include "engine/simulation_services.h"
#include "input/input_state.h"
#include "ui/retained_mui.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace snt::assets {
class AssetManager;
}
namespace snt::input {
class InputSystem;
}
namespace snt::voxel {
class ChunkRenderSystem;
}

namespace snt::engine {

class ClientRuntime;

struct ClientRuntimeStats {
    float fps = 0.0f;
    float frame_ms = 0.0f;
    float tps = 0.0f;
    float mspt = 0.0f;
    int32_t job_workers = 0;
};

class ClientWorldSession {
public:
    SimulationWorldSession& simulation() const noexcept;
    snt::ecs::World& world() const noexcept;
    snt::voxel::ChunkRegistry& chunks() const noexcept;
    snt::ecs::EventBus& events() const noexcept;

    snt::assets::AssetManager& assets() const noexcept;
    snt::input::InputSystem& input() const noexcept;
    snt::voxel::ChunkRenderSystem& chunk_render_system() const noexcept;

    snt::core::Expected<void> set_active_camera(snt::ecs::EntityGuid guid);
    snt::core::Expected<void> set_mouse_locked(bool locked);

private:
    friend class ClientRuntime;

    explicit ClientWorldSession(ClientRuntime& runtime) : runtime_(&runtime) {}

    ClientRuntime* runtime_ = nullptr;
};

class ClientFrameContext {
public:
    SimulationServices& services() const noexcept { return *services_; }
    ClientWorldSession& world() const noexcept { return *world_; }
    const snt::input::InputState& input() const noexcept;
    const ClientRuntimeStats& stats() const noexcept { return *stats_; }
    float delta_seconds() const noexcept { return delta_seconds_; }
    bool mouse_locked() const noexcept;
    void set_mouse_locked(bool locked);

private:
    friend class ClientRuntime;

    ClientFrameContext(ClientRuntime& runtime,
                       SimulationServices& services,
                       ClientWorldSession& world,
                       const ClientRuntimeStats& stats,
                       float delta_seconds)
        : runtime_(&runtime), services_(&services), world_(&world), stats_(&stats),
          delta_seconds_(delta_seconds) {}

    ClientRuntime* runtime_ = nullptr;
    SimulationServices* services_ = nullptr;
    ClientWorldSession* world_ = nullptr;
    const ClientRuntimeStats* stats_ = nullptr;
    float delta_seconds_ = 0.0f;
};

class ClientUiContext {
public:
    SimulationServices& services() const noexcept { return *services_; }
    ClientWorldSession& world() const noexcept { return *world_; }
    float viewport_width() const noexcept { return viewport_width_; }
    float viewport_height() const noexcept { return viewport_height_; }
    bool mouse_locked() const noexcept;

    // Context owns each root until the host has laid out, routed input, and
    // rendered every layer. Callers transfer ownership deliberately so UI
    // callbacks cannot observe a destroyed temporary view during dispatch.
    void submit(std::unique_ptr<snt::ui::View> root,
                snt::ui::UiLayer layer = snt::ui::UiLayer::Screen);
    void submit(snt::ui::Arc2DCommandBuffer commands,
                snt::ui::UiLayer layer = snt::ui::UiLayer::Hud);

private:
    friend class ClientRuntime;

    struct Submission {
        snt::ui::UiLayer layer = snt::ui::UiLayer::Hud;
        uint64_t order = 0;
        std::unique_ptr<snt::ui::View> root;
        std::unique_ptr<snt::ui::Arc2DCommandBuffer> commands;
    };

    ClientUiContext(ClientRuntime& runtime,
                    SimulationServices& services,
                    ClientWorldSession& world,
                    float viewport_width,
                    float viewport_height)
        : runtime_(&runtime), services_(&services), world_(&world),
          viewport_width_(viewport_width), viewport_height_(viewport_height) {}

    void flush();

    ClientRuntime* runtime_ = nullptr;
    SimulationServices* services_ = nullptr;
    ClientWorldSession* world_ = nullptr;
    float viewport_width_ = 0.0f;
    float viewport_height_ = 0.0f;
    uint64_t next_submission_order_ = 0;
    std::vector<Submission> submissions_;
};

}  // namespace snt::engine
