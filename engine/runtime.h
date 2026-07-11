// Runtime: game-agnostic lifecycle owner for the SNT engine.
//
// Runtime owns platform, graphics, ECS infrastructure, and shared services.
// An IGameSession supplies all game content and must be provided at init.

#pragma once

#include "core/clock.h"
#include "core/expected.h"
#include "core/path_utils.h"
#include "core/runtime_config.h"

#include <memory>

namespace snt::ecs { struct EntityGuid; }
namespace snt::ui {
class Arc2DCommandBuffer;
class View;
}

namespace snt::engine {

class IGameSession;

class Runtime {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    snt::core::Expected<void> init(const snt::core::RuntimeConfig& config,
                                   snt::core::RuntimePaths runtime_paths,
                                   std::unique_ptr<IGameSession> session);
    void run();
    void shutdown();

    snt::core::IClock& get_clock();
    snt::core::TimePoint get_time();
    void set_clock(snt::core::IClock* clock);

private:
    friend class FrameContext;
    friend class UiContext;
    friend class WorldSession;

    struct Impl;

    bool mouse_locked() const noexcept;
    void set_mouse_locked(bool locked);
    snt::core::Expected<void> set_active_camera(snt::ecs::EntityGuid guid);
    void submit_ui(snt::ui::View& root);
    void submit_ui(const snt::ui::Arc2DCommandBuffer& commands);

    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::engine
