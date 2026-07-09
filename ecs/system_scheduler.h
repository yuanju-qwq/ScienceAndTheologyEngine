// SystemScheduler — schedules ECS systems onto the Job System.
//
// P2 task 6: wraps snt::core::JobSystem to provide frequency-based scheduling.
//
// System groups:
//   - MAIN_THREAD (60Hz): render, input, camera. Must run on the engine
//     thread because they touch Vulkan/GDI/window handles.
//   - WORKER_POOL (20Hz/10Hz): AI, physics, ecosystem. Run on worker threads
//     via the Job System; frequency is controlled by tick accumulation.
//   - ASYNC (event-driven): resource loading, mesh baking. Triggered by
//     events, not polled.
//
// Usage:
//   SystemScheduler sched(&job_system);
//   sched.add_mainThread(std::make_shared<RenderSystem>());
//   sched.add_worker(std::make_shared<PhysicsSystem>(), 20);  // 20Hz
//   sched.update(world, dt);   // call once per frame
//
// Thread safety: single-threaded (main thread). The scheduler dispatches
// work to the Job System but the scheduler itself is only touched by the
// main thread.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/job_system.h"
#include "ecs/system.h"
#include "ecs/world.h"

namespace snt::ecs {

// Update frequency group. Controls how often a system is ticked.
enum class ScheduleGroup : uint8_t {
    MAIN_THREAD   = 0,  // 60Hz, runs on the engine thread
    WORKER_POOL   = 1,  // 20Hz or 10Hz, runs on worker threads
    ASYNC         = 2,  // event-driven, not polled
};

// A system entry in the scheduler. Tracks the target frequency and
// accumulated time since the last tick.
struct ScheduledSystem {
    std::shared_ptr<System> system;
    std::string name;
    ScheduleGroup group = ScheduleGroup::MAIN_THREAD;
    int32_t target_hz = 60;          // ticks per second
    double accumulator_sec = 0.0;    // accumulated time since last tick
    bool enabled = true;
};

// SystemScheduler — drives system updates based on frequency and group.
class SystemScheduler {
public:
    explicit SystemScheduler(snt::core::JobSystem* job_system = nullptr);
    ~SystemScheduler() = default;

    // Disallow copy.
    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;

    // --- Registration ---

    // Add a system that must run on the main thread (render, input, etc.).
    void add_main_thread(std::shared_ptr<System> system,
                         std::string name = "",
                         int32_t target_hz = 60);

    // Add a system that runs on the worker pool at a target frequency.
    // Common values: 20 (AI), 10 (ecosystem), 30 (physics).
    void add_worker(std::shared_ptr<System> system,
                    std::string name = "",
                    int32_t target_hz = 20);

    // Add an async (event-driven) system. It is not ticked by update();
    // callers invoke trigger_async() manually when the event fires.
    void add_async(std::shared_ptr<System> system,
                   std::string name = "");

    // --- Update loop ---

    // Advance the scheduler by dt seconds. Dispatches main-thread systems
    // inline, and submits worker-pool systems to the Job System.
    void update(World& world, float dt);

    // Trigger an async system by name. Submits it to the Job System.
    void trigger_async(World& world, const std::string& name, float dt = 0.0f);

    // --- Introspection ---

    size_t system_count() const { return systems_.size(); }

    // Returns a snapshot of all scheduled systems (for debugging/UI).
    const std::vector<ScheduledSystem>& systems() const { return systems_; }
    std::vector<ScheduledSystem>& systems() { return systems_; }

private:
    // Resolve the job system to use: the one passed at construction, or
    // the global default. nullptr only if called before JobSystem init.
    snt::core::JobSystem* resolve_job_system() const;

    snt::core::JobSystem* job_system_;
    std::vector<ScheduledSystem> systems_;
};

}  // namespace snt::ecs
