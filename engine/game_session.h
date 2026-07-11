// Game-session contract owned and implemented by the embedding game.
//
// Runtime owns platform, rendering, and shared services. A session owns game
// content registration, world composition, game input/UI decisions, and all
// gameplay-specific shutdown work.

#pragma once

#include "core/expected.h"

namespace snt::engine {

class FixedTickContext;
class FrameContext;
class RuntimeServices;
class UiContext;
class WorldSession;

class IGameSession {
public:
    virtual ~IGameSession() = default;

    virtual snt::core::Expected<void> register_content(RuntimeServices& services) = 0;
    virtual snt::core::Expected<void> create_world(WorldSession& world) = 0;
    virtual void fixed_tick(FixedTickContext& context) = 0;
    virtual void frame(FrameContext& context) = 0;
    virtual void build_ui(UiContext& context) = 0;
    virtual void shutdown() noexcept = 0;
};

}  // namespace snt::engine
