// Job System — P1 stub (serial) implementation.
//
// All jobs run synchronously on the calling thread. This validates the
// API design without introducing multi-threading complexity. P2 will
// replace this with a real thread pool + work-stealing queues.

#include "job_system.h"

#include <thread>

namespace snt::core {

// ---------------------------------------------------------------------------
// JobHandle
// ---------------------------------------------------------------------------

bool JobHandle::is_done() const {
    if (!counter_) return true;
    return counter_->value.load(std::memory_order_acquire) == 0;
}

void JobHandle::wait() const {
    if (!counter_) return;
    // P1 stub: busy-wait is fine since jobs run synchronously on caller.
    // P2 will use a condition variable / futex for real blocking.
    while (!is_done()) {
        std::this_thread::yield();
    }
}

// ---------------------------------------------------------------------------
// JobSystem
// ---------------------------------------------------------------------------

void JobSystem::init(int32_t worker_count) {
    // P1 stub: ignore worker_count, always single-threaded.
    worker_count_ = 1;
}

void JobSystem::shutdown() {
    // No-op in P1 (no threads to join).
}

JobHandle JobSystem::submit(JobFunc func, std::span<JobHandle> dependencies) {
    // Wait for all dependencies first (serial semantics).
    for (auto& dep : dependencies) {
        dep.wait();
    }

    auto counter = std::make_shared<JobHandle::Counter>();
    counter->value.store(1, std::memory_order_release);

    // P1 stub: run synchronously on caller thread.
    // P2: enqueue to worker pool; dependencies tracked in graph.
    const int32_t thread_index = 0;
    const int32_t tile_index = 0;
    func(thread_index, tile_index);

    counter->value.store(0, std::memory_order_release);
    return JobHandle{counter};
}

JobHandle JobSystem::parallel_for(int32_t count, JobFunc func,
                                  int32_t batch_size) {
    if (count <= 0) return JobHandle{};

    auto counter = std::make_shared<JobHandle::Counter>();
    counter->value.store(count, std::memory_order_release);

    // P1 stub: run all tiles synchronously on caller thread.
    // P2: split tiles across worker threads.
    const int32_t thread_index = 0;
    for (int32_t i = 0; i < count; ++i) {
        func(thread_index, i);
    }

    counter->value.store(0, std::memory_order_release);
    return JobHandle{counter};
}

int32_t JobSystem::worker_count() const {
    return worker_count_;
}

// ---------------------------------------------------------------------------
// Global default instance
// ---------------------------------------------------------------------------

JobSystem& default_job_system() {
    static JobSystem instance;
    static bool initialized = false;
    if (!initialized) {
        instance.init();
        initialized = true;
    }
    return instance;
}

}  // namespace snt::core
