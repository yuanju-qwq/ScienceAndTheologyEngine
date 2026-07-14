// SimulationRuntime: SDL/Vulkan-free deterministic game-runtime owner.
//
// It owns paths, file logging, content source/catalog, jobs, scripts, ECS,
// generic voxel chunks, and fixed-tick orchestration. ClientRuntime composes
// this class for presentation, while a dedicated server links this target
// directly and never receives client-only headers or libraries.

#pragma once

#include "core/clock.h"
#include "core/expected.h"
#include "core/path_utils.h"
#include "core/runtime_config.h"

#include <cstdint>
#include <memory>

namespace snt::engine {

class ClientRuntime;
class ISimulationSession;
class SimulationServices;
class SimulationWorldSession;
struct SimulationStats;

class SimulationRuntime {
public:
    SimulationRuntime();
    ~SimulationRuntime();

    SimulationRuntime(const SimulationRuntime&) = delete;
    SimulationRuntime& operator=(const SimulationRuntime&) = delete;

    snt::core::Expected<void> init(const snt::core::RuntimeConfig& config,
                                   snt::core::RuntimePaths runtime_paths,
                                   std::unique_ptr<ISimulationSession> session);

    // Advance exactly up to tick_count deterministic ticks. A session may
    // request an earlier stop through FixedTickContext::request_stop().
    [[nodiscard]] snt::core::Expected<void> run_fixed_ticks(uint64_t tick_count);

    // Advance using elapsed monotonic time, applying the fixed-tick catch-up
    // policy. Hosts that own a scheduler can use this instead of run().
    [[nodiscard]] snt::core::Expected<void> advance_time(snt::core::DurationMs elapsed);
    void run();
    void shutdown();

    void request_stop() noexcept;
    [[nodiscard]] bool stop_requested() const noexcept;
    [[nodiscard]] const SimulationStats& stats() const noexcept;
    [[nodiscard]] SimulationServices& services() noexcept;
    [[nodiscard]] SimulationWorldSession& world_session() noexcept;

    snt::core::IClock& clock();
    void set_clock(snt::core::IClock* clock);

private:
    friend class ClientRuntime;
    friend class FixedTickContext;
    friend class SimulationWorldSession;

    struct Impl;

    // ClientRuntime uses this split initialization only to establish its
    // presentation services before game callbacks create client resources.
    snt::core::Expected<void> init_services(const snt::core::RuntimeConfig& config,
                                             snt::core::RuntimePaths runtime_paths);
    snt::core::Expected<void> attach_session(std::unique_ptr<ISimulationSession> session);
    snt::core::Expected<void> run_one_fixed_tick();
    void shutdown_session() noexcept;
    void shutdown_execution() noexcept;
    void shutdown_services() noexcept;

    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::engine
