// SimulationRuntime observer contract: value-only diagnostics snapshots.
//
// Subscription and callback cadence are intentionally deferred until an
// editor or diagnostics host needs them. The contract is simulation-owned so
// a server can observe lifecycle/performance without linking client code.

#pragma once

#include "engine/simulation_services.h"

#include <cstdint>

namespace snt::engine {

enum class SimulationRuntimeLifecyclePhase : uint8_t {
    Created,
    Initializing,
    Running,
    ShuttingDown,
    Stopped,
    Failed,
};

struct SimulationRuntimeObserverSnapshot {
    SimulationRuntimeLifecyclePhase phase = SimulationRuntimeLifecyclePhase::Created;
    SimulationStats stats{};
    uint64_t fixed_tick_index = 0;
};

class ISimulationRuntimeObserver {
public:
    virtual ~ISimulationRuntimeObserver() = default;

    virtual void on_lifecycle_changed(
        const SimulationRuntimeObserverSnapshot& snapshot) = 0;
    virtual void on_statistics_updated(
        const SimulationRuntimeObserverSnapshot& snapshot) = 0;
};

}  // namespace snt::engine
