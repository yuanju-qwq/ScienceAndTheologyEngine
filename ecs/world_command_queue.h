// World command queue -- worker-to-main-thread ECS mutation boundary.
//
// Worker tasks enqueue pure commands with a deterministic producer order.
// SystemScheduler waits for every worker task at the fixed-tick barrier, then
// applies the commands on the main thread while World is exclusively owned.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "core/job_system.h"

namespace snt::ecs {

class World;

using WorldCommand = std::function<void(World&)>;

class IWorldCommandQueue {
public:
    virtual ~IWorldCommandQueue() = default;

    virtual void enqueue(uint32_t producer_order,
                         uint64_t producer_sequence,
                         WorldCommand command) = 0;
    virtual size_t pending_count() const = 0;
};

class WorldCommandQueue final : public IWorldCommandQueue {
public:
    void enqueue(uint32_t producer_order,
                 uint64_t producer_sequence,
                 WorldCommand command) override;
    size_t pending_count() const override;

    // Main-thread only. Commands are sorted by system registration order and
    // per-task sequence so worker completion timing cannot change simulation.
    size_t apply(World& world);
    void clear();

private:
    struct PendingCommand {
        uint32_t producer_order = 0;
        uint64_t producer_sequence = 0;
        WorldCommand command;
    };

    mutable std::mutex mutex_;
    std::vector<PendingCommand> commands_;
};

// Aggregate work performed by one IWorkerTask through the controlled
// parallel-compute entry point. SystemScheduler folds these values into its
// main-thread diagnostics after the fixed-tick barrier.
struct WorkerTaskParallelStats {
    uint64_t parallel_for_calls = 0;
    uint64_t parallel_for_items = 0;
};

// Passed to exactly one IWorkerTask::execute call. The local sequence counter
// is intentionally non-atomic because only the parent task may enqueue World
// commands. Child jobs created through parallel_for() must do pure computation
// into disjoint caller-owned result slots; after parallel_for() returns, the
// parent task serially enqueues its deterministic command sequence.
class WorkerCommandContext {
public:
    WorkerCommandContext(IWorldCommandQueue& queue,
                         snt::core::JobSystem& job_system,
                         uint32_t producer_order)
        : queue_(queue), job_system_(job_system), producer_order_(producer_order) {}

    void enqueue(WorldCommand command);

    // Synchronously split [0, count) into worker jobs and wait for them.
    // `func` receives the worker index and one item index. It must not retain
    // World/registry pointers or invoke enqueue() from a child job.
    void parallel_for(int32_t count,
                      snt::core::JobFunc func,
                      int32_t batch_size = 1);

    [[nodiscard]] WorkerTaskParallelStats parallel_stats() const {
        return parallel_stats_;
    }

private:
    IWorldCommandQueue& queue_;
    snt::core::JobSystem& job_system_;
    uint32_t producer_order_ = 0;
    uint64_t next_sequence_ = 0;
    WorkerTaskParallelStats parallel_stats_;
};

}  // namespace snt::ecs
