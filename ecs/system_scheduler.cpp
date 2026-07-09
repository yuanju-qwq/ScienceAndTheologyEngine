// SystemScheduler implementation.
//
// P2 task 6: schedules ECS systems onto the Job System.
//
// Dispatch policy:
//   - MAIN_THREAD systems run inline on the calling thread (the engine
//     thread) — they must not be submitted to the pool because they touch
//     window/render state.
//   - WORKER_POOL systems are submitted to the Job System via submit().
//     Each system's update() runs on a worker thread. The scheduler does
//     NOT wait for them here (fire-and-forget); callers that need to
//     synchronize should use the Job System directly.
//   - ASYNC systems are only run via trigger_async() (event-driven).

#include "system_scheduler.h"

#include "core/job_system.h"

namespace snt::ecs {

SystemScheduler::SystemScheduler(snt::core::JobSystem* job_system)
    : job_system_(job_system) {}

void SystemScheduler::add_main_thread(std::shared_ptr<System> system,
                                      std::string name,
                                      int32_t target_hz) {
    systems_.push_back(ScheduledSystem{
        std::move(system),
        std::move(name),
        ScheduleGroup::MAIN_THREAD,
        target_hz,
        0.0,
        true,
    });
}

void SystemScheduler::add_worker(std::shared_ptr<System> system,
                                 std::string name,
                                 int32_t target_hz) {
    systems_.push_back(ScheduledSystem{
        std::move(system),
        std::move(name),
        ScheduleGroup::WORKER_POOL,
        target_hz,
        0.0,
        true,
    });
}

void SystemScheduler::add_async(std::shared_ptr<System> system,
                                std::string name) {
    systems_.push_back(ScheduledSystem{
        std::move(system),
        std::move(name),
        ScheduleGroup::ASYNC,
        0,       // not polled
        0.0,
        true,
    });
}

void SystemScheduler::update(World& world, float dt) {
    snt::core::JobSystem* js = resolve_job_system();

    for (auto& entry : systems_) {
        if (!entry.enabled) continue;

        // Async systems are skipped — they only run via trigger_async().
        if (entry.group == ScheduleGroup::ASYNC) continue;

        // Frequency control via accumulator. Systems only tick when enough
        // time has accumulated to match their target_hz.
        entry.accumulator_sec += static_cast<double>(dt);
        const double tick_interval = entry.target_hz > 0
            ? (1.0 / static_cast<double>(entry.target_hz))
            : 0.0;

        if (entry.accumulator_sec < tick_interval) {
            continue;
        }

        // Consume one tick's worth of time.
        entry.accumulator_sec -= tick_interval;

        // MAIN_THREAD: run inline. These systems touch render/window state
        // and must not migrate to worker threads.
        if (entry.group == ScheduleGroup::MAIN_THREAD) {
            entry.system->update(world, dt);
            continue;
        }

        // WORKER_POOL: submit to the Job System. Use a raw pointer copy
        // (the shared_ptr is kept alive by the systems_ vector).
        if (js != nullptr) {
            System* sys = entry.system.get();
            js->submit([sys, &world, dt](int32_t, int32_t) {
                sys->update(world, dt);
            });
        } else {
            // Fallback: run inline if no Job System is available.
            entry.system->update(world, dt);
        }
    }
}

void SystemScheduler::trigger_async(World& world, const std::string& name,
                                    float dt) {
    snt::core::JobSystem* js = resolve_job_system();

    for (auto& entry : systems_) {
        if (entry.group != ScheduleGroup::ASYNC) continue;
        if (entry.name != name) continue;

        if (js != nullptr) {
            System* sys = entry.system.get();
            js->submit([sys, &world, dt](int32_t, int32_t) {
                sys->update(world, dt);
            });
        } else {
            entry.system->update(world, dt);
        }
        return;
    }
}

snt::core::JobSystem* SystemScheduler::resolve_job_system() const {
    if (job_system_ != nullptr) {
        return job_system_;
    }
    return &snt::core::default_job_system();
}

}  // namespace snt::ecs
