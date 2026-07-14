// SimulationRuntime service and world-session contracts.
//
// SimulationRuntime owns all objects behind these non-owning views. The
// exposed surface is deliberately Vulkan/SDL-free: paths, content source and
// catalog, clock, logging, jobs, scripts, ECS World, and generic voxel data.

#pragma once

#include "core/expected.h"
#include "core/path_utils.h"
#include "core/runtime_config.h"
#include "ecs/entity_guid.h"
#include "ecs/event_bus.h"
#include "ecs/system_scheduler.h"

#include <cstdint>
#include <memory>

namespace snt::assets {
class AssetCatalog;
class IAssetSource;
}
namespace snt::core {
class IClock;
class JobSystem;
class Logger;
}
namespace snt::ecs {
class World;
}
namespace snt::script {
class ScriptManager;
}
namespace snt::voxel {
class ChunkRegistry;
}

namespace snt::engine {

class ClientRuntime;
class SimulationRuntime;

class SimulationServices {
public:
    const snt::core::RuntimeConfig& config() const noexcept;
    const snt::core::RuntimePathResolver& paths() const noexcept;
    snt::core::IClock& clock() const noexcept;
    snt::core::Logger& logger() const noexcept;
    snt::core::JobSystem& jobs() const noexcept;

    // The source owns one explicit game-content root and can be used by a
    // loading worker. Catalog lookup is immutable after runtime startup.
    snt::assets::IAssetSource& content_source() const noexcept;
    const snt::assets::AssetCatalog& asset_catalog() const noexcept;
    snt::script::ScriptManager& scripts() const noexcept;

private:
    friend class SimulationRuntime;

    SimulationServices(const snt::core::RuntimeConfig& config,
                       const snt::core::RuntimePathResolver& paths,
                       snt::core::IClock& clock,
                       snt::core::Logger& logger,
                       snt::core::JobSystem& jobs,
                       snt::assets::IAssetSource& content_source,
                       const snt::assets::AssetCatalog& asset_catalog,
                       snt::script::ScriptManager& scripts);

    const snt::core::RuntimeConfig* config_ = nullptr;
    const snt::core::RuntimePathResolver* paths_ = nullptr;
    snt::core::IClock* clock_ = nullptr;
    snt::core::Logger* logger_ = nullptr;
    snt::core::JobSystem* jobs_ = nullptr;
    snt::assets::IAssetSource* content_source_ = nullptr;
    const snt::assets::AssetCatalog* asset_catalog_ = nullptr;
    snt::script::ScriptManager* scripts_ = nullptr;
};

class SimulationWorldSession {
public:
    snt::ecs::World& world() const noexcept;
    snt::voxel::ChunkRegistry& chunks() const noexcept;
    snt::ecs::EventBus& events() const noexcept;

    // SimulationRuntime owns the scheduler. Session composition remains
    // main-thread-only and never attaches systems directly to World.
    [[nodiscard]] snt::core::Expected<snt::ecs::SystemHandle> register_main_system(
        std::shared_ptr<snt::ecs::System> system);
    [[nodiscard]] snt::core::Expected<snt::ecs::SystemHandle> register_worker_system(
        std::shared_ptr<snt::ecs::IWorkerSystem> system);
    [[nodiscard]] snt::core::Expected<void> set_system_enabled(
        snt::ecs::SystemHandle handle, bool enabled);

private:
    friend class SimulationRuntime;
    friend class ClientRuntime;

    explicit SimulationWorldSession(SimulationRuntime& runtime) : runtime_(&runtime) {}

    SimulationRuntime* runtime_ = nullptr;
};

struct SimulationStats {
    float tps = 0.0f;
    float mspt = 0.0f;
    int32_t job_workers = 0;
};

class FixedTickContext {
public:
    SimulationServices& services() const noexcept { return *services_; }
    SimulationWorldSession& world() const noexcept { return *world_; }
    float delta_seconds() const noexcept { return delta_seconds_; }
    uint64_t tick_index() const noexcept { return tick_index_; }

    // Ends a long-running SimulationRuntime::run() loop after this tick.
    // It is also used by bounded tests and future server orchestration.
    void request_stop() const noexcept;

private:
    friend class SimulationRuntime;

    FixedTickContext(SimulationRuntime& runtime,
                     SimulationServices& services,
                     SimulationWorldSession& world,
                     float delta_seconds,
                     uint64_t tick_index)
        : runtime_(&runtime), services_(&services), world_(&world),
          delta_seconds_(delta_seconds), tick_index_(tick_index) {}

    SimulationRuntime* runtime_ = nullptr;
    SimulationServices* services_ = nullptr;
    SimulationWorldSession* world_ = nullptr;
    float delta_seconds_ = 0.0f;
    uint64_t tick_index_ = 0;
};

}  // namespace snt::engine
