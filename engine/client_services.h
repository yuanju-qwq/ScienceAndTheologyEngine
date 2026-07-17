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
#include "ui/retained_mui_screen_stack.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace snt::assets {
class AssetManager;
}
namespace snt::ui {
class UiImageRegistry;
}
namespace snt::ui::mod {
class IModUiRuntime;
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
    snt::ui::UiImageRegistry& ui_images() const noexcept;
    snt::ui::UiLayerStack& ui_layers() const noexcept;
    // Package loaders use this narrow facade to attach Mod UI. It never
    // exposes retained Views, renderer handles, ECS, or client internals.
    snt::ui::mod::IModUiRuntime& mod_ui() const noexcept;
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
    float viewport_width() const noexcept { return viewport_.logical_size().x; }
    float viewport_height() const noexcept { return viewport_.logical_size().y; }
    const snt::ui::UiViewport& viewport() const noexcept { return viewport_; }
    bool mouse_locked() const noexcept;
    snt::ui::UiImageRegistry& images() const noexcept;
    snt::ui::UiLayerStack& layers() const noexcept;

    // Retained Views are registered through UiLayerStack. This context only
    // accepts stateless Arc2D overlays such as crosshairs and debug marks;
    // allowing temporary View roots here would bypass focus, capture, and
    // retained ownership guarantees.
    void submit(snt::ui::Arc2DCommandBuffer commands,
                snt::ui::UiLayer layer = snt::ui::UiLayer::Hud);

private:
    friend class ClientRuntime;

    struct Submission {
        snt::ui::UiLayer layer = snt::ui::UiLayer::Hud;
        snt::ui::UiLayerInputPolicy input_policy{};
        uint64_t order = 0;
        // Layer-stack-owned screens keep a retained root across frames. Direct
        // session submissions are Arc2D-only and never carry a View tree.
        snt::ui::View* borrowed_root = nullptr;
        std::unique_ptr<snt::ui::Arc2DCommandBuffer> commands;

        [[nodiscard]] snt::ui::View* view_root() const noexcept {
            return borrowed_root;
        }
    };

    ClientUiContext(ClientRuntime& runtime,
                    SimulationServices& services,
                    ClientWorldSession& world,
                    snt::ui::UiViewport viewport,
                    float delta_seconds)
        : runtime_(&runtime), services_(&services), world_(&world),
          viewport_(std::move(viewport)), delta_seconds_(delta_seconds) {}

    void flush();

    ClientRuntime* runtime_ = nullptr;
    SimulationServices* services_ = nullptr;
    ClientWorldSession* world_ = nullptr;
    snt::ui::UiViewport viewport_{};
    float delta_seconds_ = 0.0f;
    uint64_t next_submission_order_ = 0;
    std::vector<Submission> submissions_;
    // Reused while routing one retained input frame. Keeping this separate
    // from submissions avoids granting Arc2D-only overlays a UI lifecycle.
    std::vector<snt::ui::View*> active_roots_;
};

}  // namespace snt::engine
