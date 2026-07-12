// Runtime service and callback contracts.
//
// Runtime owns the objects behind these non-owning views. IGameSession may use
// them only on the main runtime thread and must not retain callback contexts.

#pragma once

#include "core/expected.h"
#include "core/path_utils.h"
#include "core/runtime_config.h"
#include "ecs/entity_guid.h"
#include "ecs/event_bus.h"
#include "ecs/system_scheduler.h"
#include "input/input_state.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace snt::assets { class AssetManager; }
namespace snt::core {
class IClock;
class JobSystem;
class Logger;
}
namespace snt::data { class ChunkRegistry; }
namespace snt::ecs {
class World;
}
namespace snt::engine { class Runtime; }
namespace snt::input {
class InputSystem;
}
namespace snt::script { class ScriptManager; }
namespace snt::ui {
class Arc2DCommandBuffer;
class View;
}
namespace snt::voxel { class ChunkRenderSystem; }

namespace snt::engine {

class RuntimeServices {
public:
    const snt::core::RuntimeConfig& config() const noexcept;
    // Runtime-owned, immutable path resolver. The returned reference is valid
    // only while this RuntimeServices instance is alive.
    const snt::core::RuntimePathResolver& paths() const noexcept;
    snt::core::IClock& clock() const noexcept;
    snt::core::Logger& logger() const noexcept;
    snt::core::JobSystem& jobs() const noexcept;
    snt::assets::AssetManager& assets() const noexcept;
    snt::script::ScriptManager& scripts() const noexcept;

private:
    friend class Runtime;

    RuntimeServices(const snt::core::RuntimeConfig& config,
                    const snt::core::RuntimePathResolver& paths,
                    snt::core::IClock& clock,
                    snt::core::Logger& logger,
                    snt::core::JobSystem& jobs,
                    snt::assets::AssetManager& assets,
                    snt::script::ScriptManager& scripts);

    const snt::core::RuntimeConfig* config_ = nullptr;
    const snt::core::RuntimePathResolver* paths_ = nullptr;
    snt::core::IClock* clock_ = nullptr;
    snt::core::Logger* logger_ = nullptr;
    snt::core::JobSystem* jobs_ = nullptr;
    snt::assets::AssetManager* assets_ = nullptr;
    snt::script::ScriptManager* scripts_ = nullptr;
};

class WorldSession {
public:
    snt::ecs::World& world() const noexcept;
    snt::data::ChunkRegistry& chunks() const noexcept;
    snt::voxel::ChunkRenderSystem& chunk_render_system() const noexcept;
    snt::ecs::EventBus& events() const noexcept;
    snt::input::InputSystem& input() const noexcept;

    // Runtime owns the scheduler; sessions compose their systems through
    // these registration APIs instead of attaching them to World directly.
    // Registration is main-thread-only and returns the stable handle used to
    // enable or disable a system later in the session lifetime.
    [[nodiscard]] snt::core::Expected<snt::ecs::SystemHandle> register_main_system(
        std::shared_ptr<snt::ecs::System> system);
    [[nodiscard]] snt::core::Expected<snt::ecs::SystemHandle> register_worker_system(
        std::shared_ptr<snt::ecs::IWorkerSystem> system);
    [[nodiscard]] snt::core::Expected<void> set_system_enabled(
        snt::ecs::SystemHandle handle, bool enabled);

    snt::core::Expected<void> set_active_camera(snt::ecs::EntityGuid guid);
    void set_mouse_locked(bool locked);

private:
    friend class Runtime;

    explicit WorldSession(Runtime& runtime) : runtime_(&runtime) {}

    Runtime* runtime_ = nullptr;
};

struct RuntimeStats {
    float fps = 0.0f;
    float frame_ms = 0.0f;
    float tps = 0.0f;
    float mspt = 0.0f;
    int32_t job_workers = 0;
};

class FixedTickContext {
public:
    RuntimeServices& services() const noexcept { return *services_; }
    WorldSession& world() const noexcept { return *world_; }
    float delta_seconds() const noexcept { return delta_seconds_; }
    uint64_t tick_index() const noexcept { return tick_index_; }

private:
    friend class Runtime;

    FixedTickContext(RuntimeServices& services, WorldSession& world,
                     float delta_seconds, uint64_t tick_index)
        : services_(&services), world_(&world), delta_seconds_(delta_seconds),
          tick_index_(tick_index) {}

    RuntimeServices* services_ = nullptr;
    WorldSession* world_ = nullptr;
    float delta_seconds_ = 0.0f;
    uint64_t tick_index_ = 0;
};

class FrameContext {
public:
    RuntimeServices& services() const noexcept { return *services_; }
    WorldSession& world() const noexcept { return *world_; }
    const snt::input::InputState& input() const noexcept;
    const RuntimeStats& stats() const noexcept { return *stats_; }
    float delta_seconds() const noexcept { return delta_seconds_; }
    bool mouse_locked() const noexcept;
    void set_mouse_locked(bool locked);

private:
    friend class Runtime;

    FrameContext(Runtime& runtime, RuntimeServices& services, WorldSession& world,
                 const RuntimeStats& stats, float delta_seconds)
        : runtime_(&runtime), services_(&services), world_(&world), stats_(&stats),
          delta_seconds_(delta_seconds) {}

    Runtime* runtime_ = nullptr;
    RuntimeServices* services_ = nullptr;
    WorldSession* world_ = nullptr;
    const RuntimeStats* stats_ = nullptr;
    float delta_seconds_ = 0.0f;
};

class UiContext {
public:
    RuntimeServices& services() const noexcept { return *services_; }
    WorldSession& world() const noexcept { return *world_; }
    float viewport_width() const noexcept { return viewport_width_; }
    float viewport_height() const noexcept { return viewport_height_; }
    bool mouse_locked() const noexcept;

    void submit(snt::ui::View& root);
    void submit(const snt::ui::Arc2DCommandBuffer& commands);

private:
    friend class Runtime;

    UiContext(Runtime& runtime, RuntimeServices& services, WorldSession& world,
              float viewport_width, float viewport_height)
        : runtime_(&runtime), services_(&services), world_(&world),
          viewport_width_(viewport_width), viewport_height_(viewport_height) {}

    Runtime* runtime_ = nullptr;
    RuntimeServices* services_ = nullptr;
    WorldSession* world_ = nullptr;
    float viewport_width_ = 0.0f;
    float viewport_height_ = 0.0f;
};

}  // namespace snt::engine
