// World command queue implementation.

#include "ecs/world_command_queue.h"

#include <algorithm>
#include <utility>

#include "ecs/world.h"

namespace snt::ecs {

void WorldCommandQueue::enqueue(uint32_t producer_order,
                                uint64_t producer_sequence,
                                WorldCommand command) {
    if (!command) {
        return;
    }

    std::lock_guard lock(mutex_);
    commands_.push_back(PendingCommand{
        producer_order,
        producer_sequence,
        std::move(command),
    });
}

size_t WorldCommandQueue::pending_count() const {
    std::lock_guard lock(mutex_);
    return commands_.size();
}

size_t WorldCommandQueue::apply(World& world) {
    std::vector<PendingCommand> pending;
    {
        std::lock_guard lock(mutex_);
        pending.swap(commands_);
    }

    std::sort(pending.begin(), pending.end(),
              [](const PendingCommand& lhs, const PendingCommand& rhs) {
                  if (lhs.producer_order != rhs.producer_order) {
                      return lhs.producer_order < rhs.producer_order;
                  }
                  return lhs.producer_sequence < rhs.producer_sequence;
              });

    for (auto& pending_command : pending) {
        pending_command.command(world);
    }
    return pending.size();
}

void WorldCommandQueue::clear() {
    std::lock_guard lock(mutex_);
    commands_.clear();
}

void WorkerCommandContext::enqueue(WorldCommand command) {
    queue_.enqueue(producer_order_, next_sequence_++, std::move(command));
}

void WorkerCommandContext::parallel_for(int32_t count,
                                        snt::core::JobFunc func,
                                        int32_t batch_size) {
    if (count <= 0 || !func) {
        return;
    }

    ++parallel_stats_.parallel_for_calls;
    parallel_stats_.parallel_for_items += static_cast<uint64_t>(count);
    job_system_.parallel_for(count, std::move(func), batch_size).wait();
}

}  // namespace snt::ecs
