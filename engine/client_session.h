// Client-session extension for platform and presentation composition.
//
// A game implements ISimulationSession for all deterministic state, then
// implements this interface only in its graphical host. A dedicated server
// receives no ClientWorldSession, frame, or UI callback.

#pragma once

#include "engine/simulation_session.h"

namespace snt::engine {

class ClientFrameContext;
class ClientUiContext;
class ClientWorldSession;

class IClientSession : public ISimulationSession {
public:
    ~IClientSession() override = default;

    virtual snt::core::Expected<void> create_client_world(ClientWorldSession& world) = 0;
    virtual void frame(ClientFrameContext& context) = 0;
    virtual void build_ui(ClientUiContext& context) = 0;
};

}  // namespace snt::engine
