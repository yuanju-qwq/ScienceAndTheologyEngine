// Runtime observer contract: read-only lifecycle and performance snapshots.
//
// This is intentionally declaration-only. Runtime subscription and callback
// cadence will be implemented when editor or diagnostics consumers exist.
// Observers receive value snapshots and never receive writable subsystem
// pointers, preserving Runtime ownership and main-thread boundaries.

#pragma once

#include "engine/runtime_services.h"

#include <cstdint>

namespace snt::engine {

enum class RuntimeLifecyclePhase : uint8_t {
    Created,
    Initializing,
    Running,
    ShuttingDown,
    Stopped,
    Failed,
};

struct RuntimeObserverSnapshot {
    RuntimeLifecyclePhase phase = RuntimeLifecyclePhase::Created;
    RuntimeStats stats{};
    uint64_t fixed_tick_index = 0;
};

class IRuntimeObserver {
public:
    virtual ~IRuntimeObserver() = default;

    // Called on the Runtime main thread after a lifecycle transition.
    // Copy the snapshot if it must outlive the callback.
    virtual void on_lifecycle_changed(const RuntimeObserverSnapshot& snapshot) = 0;

    // Called on the Runtime main thread at the future diagnostics cadence.
    virtual void on_statistics_updated(const RuntimeObserverSnapshot& snapshot) = 0;
};

}  // namespace snt::engine
