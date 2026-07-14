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
    virtual void fixed_tick(FixedTickContext& context) = 0;
    virtual void shutdown() noexcept = 0;
};

}  // namespace snt::engine
