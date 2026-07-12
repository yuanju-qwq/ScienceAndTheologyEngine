// SystemScheduler implementation.

#define SNT_LOG_CHANNEL "ecs.scheduler"
#include "ecs/system_scheduler.h"

#include <chrono>
#include <span>
#include <string>
#include <utility>

#include "core/log.h"

namespace snt::ecs {
namespace {

constexpr auto kSlowBarrierThreshold = std::chrono::milliseconds(50);
constexpr auto kSlowBarrierLogInterval = std::chrono::seconds(1);

snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, message};
}

snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, message};
}

}  // namespace

SystemScheduler::SystemScheduler(snt::core::JobSystem& job_system)
    : job_system_(job_system),
      main_thread_id_(std::this_thread::get_id()) {}

SystemScheduler::~SystemScheduler() {
    shutdown();
}

snt::core::Expected<SystemHandle> SystemScheduler::register_main(
    std::shared_ptr<System> system) {
    if (auto main_thread = validate_main_thread(); !main_thread) {
        return main_thread.error();
    }
    if (shutdown_requested_) {
        return invalid_state("SystemScheduler::register_main after shutdown");
    }
    if (!system) {
        return invalid_argument("SystemScheduler::register_main requires a system");
    }

    SystemMetadata metadata = system->metadata();
    if (auto valid = validate_metadata(metadata,
                                       SystemThreadAffinity::MainThread);
        !valid) {
        return valid.error();
    }

    const SystemHandle handle = static_cast<SystemHandle>(systems_.size());
    systems_.push_back(SystemEntry{
        ScheduledSystem{handle, std::move(metadata), false, true},
        std::move(system),
        nullptr,
    });
    return handle;
}

snt::core::Expected<SystemHandle> SystemScheduler::register_worker(
    std::shared_ptr<IWorkerSystem> system) {
    if (auto main_thread = validate_main_thread(); !main_thread) {
        return main_thread.error();
    }
    if (shutdown_requested_) {
        return invalid_state("SystemScheduler::register_worker after shutdown");
    }
    if (!system) {
        return invalid_argument("SystemScheduler::register_worker requires a system");
    }

    SystemMetadata metadata = system->metadata();
    if (auto valid = validate_metadata(metadata, SystemThreadAffinity::Worker);
        !valid) {
        return valid.error();
    }

    const SystemHandle handle = static_cast<SystemHandle>(systems_.size());
    systems_.push_back(SystemEntry{
        ScheduledSystem{handle, std::move(metadata), true, true},
        nullptr,
        std::move(system),
    });
    return handle;
}

snt::core::Expected<void> SystemScheduler::set_enabled(SystemHandle handle,
                                                        bool enabled) {
    if (auto main_thread = validate_main_thread(); !main_thread) {
        return main_thread;
    }
    if (shutdown_requested_) {
        return invalid_state("SystemScheduler::set_enabled after shutdown");
    }
    if (handle >= systems_.size()) {
        return invalid_argument("SystemScheduler::set_enabled received an invalid handle");
    }
    systems_[handle].scheduled.enabled = enabled;
    return {};
}

snt::core::Expected<void> SystemScheduler::fixed_tick(World& world, float dt) {
    if (auto main_thread = validate_main_thread(); !main_thread) {
        return main_thread;
    }
    if (shutdown_requested_) {
        return invalid_state("SystemScheduler::fixed_tick after shutdown");
    }
    if (dt < 0.0f) {
        return invalid_argument("SystemScheduler::fixed_tick requires non-negative dt");
    }

    // Direct World mutation remains deterministic and main-thread-only.
    for (auto& entry : systems_) {
        if (!entry.scheduled.enabled || entry.scheduled.worker) {
            continue;
        }
        entry.main_system->update(world, dt);
        ++diagnostics_.main_system_updates;
    }

    struct PendingWorkerTask {
        const SystemMetadata* metadata = nullptr;
        SystemHandle handle = 0;
        std::shared_ptr<IWorkerTask> task;
    };
    std::vector<PendingWorkerTask> pending_tasks;
    pending_tasks.reserve(systems_.size());

    // capture() runs after the main phase and receives only a const World.
    for (auto& entry : systems_) {
        if (!entry.scheduled.enabled || !entry.scheduled.worker) {
            continue;
        }

        std::unique_ptr<IWorkerTask> captured = entry.worker_system->capture(world, dt);
        if (!captured) {
            continue;
        }

        pending_tasks.push_back(PendingWorkerTask{
            &entry.scheduled.metadata,
            entry.scheduled.handle,
            std::shared_ptr<IWorkerTask>(std::move(captured)),
        });
    }

    const auto barrier_start = std::chrono::steady_clock::now();
    struct SubmittedWorkerTask {
        const SystemMetadata* metadata = nullptr;
        snt::core::JobHandle completion;
        std::shared_ptr<WorkerTaskParallelStats> parallel_stats;
    };
    std::vector<SubmittedWorkerTask> submitted;
    submitted.reserve(pending_tasks.size());
    in_flight_.clear();
    in_flight_.reserve(pending_tasks.size());

    for (const auto& pending : pending_tasks) {
        std::vector<snt::core::JobHandle> dependencies;
        for (const auto& prior : submitted) {
            if (resources_conflict(*prior.metadata, *pending.metadata)) {
                dependencies.push_back(prior.completion);
            }
        }
        diagnostics_.conflict_edges += dependencies.size();

        IWorldCommandQueue* queue = &command_queue_;
        snt::core::JobSystem* job_system = &job_system_;
        const SystemHandle producer_order = pending.handle;
        auto parallel_stats = std::make_shared<WorkerTaskParallelStats>();
        auto completion = job_system_.submit(
            [task = pending.task, queue, job_system, producer_order, parallel_stats]
            (int32_t, int32_t) {
                WorkerCommandContext commands(*queue, *job_system, producer_order);
                task->execute(commands);
                *parallel_stats = commands.parallel_stats();
            },
            std::span<snt::core::JobHandle>(dependencies));

        submitted.push_back(SubmittedWorkerTask{
            pending.metadata,
            completion,
            std::move(parallel_stats),
        });
        in_flight_.push_back(std::move(completion));
    }

    for (const auto& completion : in_flight_) {
        completion.wait();
    }
    in_flight_.clear();

    for (const auto& task : submitted) {
        diagnostics_.worker_parallel_for_calls += task.parallel_stats->parallel_for_calls;
        diagnostics_.worker_parallel_for_items += task.parallel_stats->parallel_for_items;
    }

    const size_t applied_commands = command_queue_.apply(world);
    ++diagnostics_.fixed_ticks;
    diagnostics_.worker_tasks_submitted += pending_tasks.size();
    diagnostics_.commands_applied += applied_commands;
    log_slow_barrier(std::chrono::steady_clock::now() - barrier_start,
                     pending_tasks.size(), applied_commands);
    return {};
}

void SystemScheduler::shutdown() noexcept {
    if (shutdown_requested_) {
        return;
    }

    for (const auto& completion : in_flight_) {
        completion.wait();
    }
    in_flight_.clear();
    command_queue_.clear();
    shutdown_requested_ = true;
}

std::vector<ScheduledSystem> SystemScheduler::systems() const {
    std::vector<ScheduledSystem> result;
    result.reserve(systems_.size());
    for (const auto& entry : systems_) {
        result.push_back(entry.scheduled);
    }
    return result;
}

snt::core::Expected<void> SystemScheduler::validate_metadata(
    const SystemMetadata& metadata,
    SystemThreadAffinity expected_affinity) const {
    if (metadata.name.empty()) {
        return invalid_argument("SystemScheduler requires a non-empty system name");
    }
    if (metadata.affinity != expected_affinity) {
        return invalid_argument("SystemScheduler received a system with the wrong thread affinity");
    }
    for (const auto& existing : systems_) {
        if (existing.scheduled.metadata.name == metadata.name) {
            return invalid_argument("SystemScheduler requires unique system names");
        }
    }
    for (const auto& resource : metadata.resources) {
        if (resource.resource.empty()) {
            return invalid_argument("SystemScheduler resource declarations require names");
        }
    }
    return {};
}

snt::core::Expected<void> SystemScheduler::validate_main_thread() const {
    if (std::this_thread::get_id() != main_thread_id_) {
        return invalid_state("SystemScheduler is main-thread-only");
    }
    return {};
}

bool SystemScheduler::resources_conflict(const SystemMetadata& lhs,
                                         const SystemMetadata& rhs) {
    for (const auto& left : lhs.resources) {
        for (const auto& right : rhs.resources) {
            if (left.resource != right.resource) {
                continue;
            }
            if (left.mode == SystemResourceAccessMode::Write ||
                right.mode == SystemResourceAccessMode::Write) {
                return true;
            }
        }
    }
    return false;
}

void SystemScheduler::log_slow_barrier(std::chrono::steady_clock::duration elapsed,
                                       size_t worker_task_count,
                                       size_t command_count) {
    if (elapsed < kSlowBarrierThreshold) {
        return;
    }

    ++diagnostics_.slow_barriers;
    const auto now = std::chrono::steady_clock::now();
    if (now - last_slow_barrier_log_ < kSlowBarrierLogInterval) {
        ++diagnostics_.suppressed_slow_barrier_warnings;
        return;
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    SNT_LOG_WARN("worker barrier took %lldms for %zu task(s), %zu command(s); slow barriers=%llu, suppressed=%llu",
                 static_cast<long long>(elapsed_ms),
                 worker_task_count,
                 command_count,
                 static_cast<unsigned long long>(diagnostics_.slow_barriers),
                 static_cast<unsigned long long>(diagnostics_.suppressed_slow_barrier_warnings));
    diagnostics_.suppressed_slow_barrier_warnings = 0;
    last_slow_barrier_log_ = now;
}

}  // namespace snt::ecs
