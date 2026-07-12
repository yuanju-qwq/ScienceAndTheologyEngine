// SystemScheduler -- fixed-tick ECS orchestration with safe worker tasks.
//
// Main systems update World directly in deterministic registration order.
// Worker systems first capture immutable input on the main thread, then run
// task objects without a World reference. Resource conflicts produce DAG
// dependencies, every task reaches a barrier before the next tick, and queued
// commands are applied on the main thread in deterministic producer order.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "core/expected.h"
#include "core/job_system.h"
#include "ecs/system.h"
#include "ecs/world.h"
#include "ecs/world_command_queue.h"

namespace snt::ecs {

using SystemHandle = uint32_t;

struct ScheduledSystem {
    SystemHandle handle = 0;
    SystemMetadata metadata;
    bool worker = false;
    bool enabled = true;
};

struct SchedulerDiagnostics {
    uint64_t fixed_ticks = 0;
    uint64_t main_system_updates = 0;
    uint64_t worker_tasks_submitted = 0;
    uint64_t worker_parallel_for_calls = 0;
    uint64_t worker_parallel_for_items = 0;
    uint64_t conflict_edges = 0;
    uint64_t commands_applied = 0;
    uint64_t slow_barriers = 0;
    uint64_t suppressed_slow_barrier_warnings = 0;
};

class SystemScheduler {
public:
    explicit SystemScheduler(snt::core::JobSystem& job_system);
    ~SystemScheduler();

    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;

    [[nodiscard]] snt::core::Expected<SystemHandle> register_main(
        std::shared_ptr<System> system);
    [[nodiscard]] snt::core::Expected<SystemHandle> register_worker(
        std::shared_ptr<IWorkerSystem> system);
    [[nodiscard]] snt::core::Expected<void> set_enabled(SystemHandle handle,
                                                         bool enabled);

    // Main-thread only. All worker work completes and all queued World
    // commands apply before this method returns, so no task crosses a tick.
    [[nodiscard]] snt::core::Expected<void> fixed_tick(World& world, float dt);

    // Main-thread only. Idempotent; waits for any tracked task before queued
    // commands are discarded, so no worker retains scheduler-owned state.
    void shutdown() noexcept;

    [[nodiscard]] size_t system_count() const { return systems_.size(); }
    [[nodiscard]] std::vector<ScheduledSystem> systems() const;
    [[nodiscard]] SchedulerDiagnostics diagnostics() const { return diagnostics_; }
    [[nodiscard]] bool is_shutdown() const { return shutdown_requested_; }

private:
    struct SystemEntry {
        ScheduledSystem scheduled;
        std::shared_ptr<System> main_system;
        std::shared_ptr<IWorkerSystem> worker_system;
    };

    [[nodiscard]] snt::core::Expected<void> validate_metadata(
        const SystemMetadata& metadata,
        SystemThreadAffinity expected_affinity) const;
    [[nodiscard]] snt::core::Expected<void> validate_main_thread() const;
    static bool resources_conflict(const SystemMetadata& lhs,
                                   const SystemMetadata& rhs);
    void log_slow_barrier(std::chrono::steady_clock::duration elapsed,
                          size_t worker_task_count,
                          size_t command_count);

    snt::core::JobSystem& job_system_;
    std::thread::id main_thread_id_;
    std::vector<SystemEntry> systems_;
    std::vector<snt::core::JobHandle> in_flight_;
    WorldCommandQueue command_queue_;
    SchedulerDiagnostics diagnostics_;
    std::chrono::steady_clock::time_point last_slow_barrier_log_{};
    bool shutdown_requested_ = false;
};

}  // namespace snt::ecs
