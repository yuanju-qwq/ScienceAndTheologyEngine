// Simulation-session contract implemented by an embedding game.
//
// This contract intentionally contains no platform, input, rendering, or GPU
// asset type. It is the only session interface accepted by SimulationRuntime,
// so a dedicated server links the same deterministic lifecycle as a client.

#pragma once

#include "core/expected.h"

namespace snt::engine {

class FixedTickContext;
class SimulationServices;
class SimulationWorldSession;

class ISimulationSession {
public:
    virtual ~ISimulationSession() = default;

    virtual snt::core::Expected<void> register_content(SimulationServices& services) = 0;
    virtual snt::core::Expected<void> create_world(SimulationWorldSession& world) = 0;
    // Runs before SystemScheduler's fixed-tick barrier. Network-aware
    // sessions poll inbound transport here, then apply deterministic game
    // commands before registered ECS systems execute.
    virtual snt::core::Expected<void> fixed_tick(FixedTickContext& context) = 0;

    // Runs after every SystemScheduler main/worker phase and command barrier.
    // Network-aware sessions emit snapshots or commands here so outbound
    // state always observes the completed authoritative tick.
    virtual snt::core::Expected<void> after_fixed_tick(FixedTickContext& context) = 0;
    virtual void shutdown() noexcept = 0;
};

}  // namespace snt::engine
