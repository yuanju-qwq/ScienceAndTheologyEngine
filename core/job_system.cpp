// Job System — P1 stub (serial) + P2 (work-stealing thread pool).
//
// Two implementations share this TU:
//   - JobSystem:        P1 serial stub, kept for reference / fallback.
//   - JobSystemP2:      P2 real thread pool with per-worker deques,
//                        work-stealing, dependency tracking and graceful
//                        shutdown. Runtime::init() installs a JobSystemP2
//                        as the default via set_default_job_system().

#include "job_system.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <thread>

namespace snt::core {

// Pointer installed by set_default_job_system(); nullptr = use built-in.
// Written once at engine init, read by every default_job_system() call.
static JobSystem* g_default_override = nullptr;

// A worker can execute nested jobs while waiting on a child handle. The
// handle uses this ownership marker to distinguish its pool from unrelated
// pools that might be active on another thread.
thread_local JobSystemP2* g_current_job_system = nullptr;
thread_local int32_t g_current_worker_index = -1;

// ---------------------------------------------------------------------------
// JobHandle
// ---------------------------------------------------------------------------

bool JobHandle::is_done() const {
    if (!counter_) return true;
    return counter_->value.load(std::memory_order_acquire) == 0;
}

void JobHandle::wait() const {
    if (!counter_) return;

    // Fast path: already complete.
    if (is_done()) return;

    if (counter_->owner != nullptr) {
        counter_->owner->wait_for_counter(counter_);
        return;
    }

    // P1 creates completed handles synchronously. Keep a yield fallback for
    // future non-P2 implementations that choose asynchronous execution.
    while (!is_done()) std::this_thread::yield();
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

// ===========================================================================
// JobSystemP2 — real multi-threaded implementation
// ===========================================================================

JobSystemP2::~JobSystemP2() {
    shutdown();
}

void JobSystemP2::init(int32_t worker_count) {
    // Default: hardware_concurrency - 1 (leave one core for the main thread
    // + render). Always at least one worker so submit() doesn't deadlock.
    if (worker_count <= 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        worker_count = (hw > 1) ? static_cast<int32_t>(hw - 1) : 1;
    }
    worker_count_ = worker_count;

    // Allocate one deque per worker. Workers are the only thread that
    // pushes/pops from the back; stealers only pop from the front, so
    // contention is limited to the steal path (rare under balanced load).
    queues_.clear();
    queues_.reserve(worker_count_);
    for (int32_t i = 0; i < worker_count; ++i) {
        queues_.push_back(std::make_unique<WorkerQueue>());
    }

    stopping_.store(false, std::memory_order_release);
    total_jobs_.store(0, std::memory_order_release);

    threads_.clear();
    threads_.reserve(worker_count_);
    for (int32_t i = 0; i < worker_count; ++i) {
        threads_.emplace_back([this, i] { worker_loop(i); });
    }
}

void JobSystemP2::shutdown() {
    // Already shut down (no threads) — nothing to do. Makes shutdown()
    // idempotent, matching the P1 stub contract.
    if (threads_.empty()) return;

    // Graceful drain: wait for all outstanding jobs (running + pending)
    // to finish BEFORE signalling workers to stop. This guarantees that
    // async resource loads submitted before shutdown complete and their
    // Counter reach 0, so any final wait() calls return promptly.
    while (total_jobs_.load(std::memory_order_acquire) > 0) {
        std::this_thread::yield();
    }

    // Wake all workers so they observe stopping_ and exit.
    stopping_.store(true, std::memory_order_release);
    global_cv_.notify_all();

    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
    queues_.clear();
    worker_count_ = 0;
}

JobHandle JobSystemP2::submit(JobFunc func,
                              std::span<JobHandle> dependencies) {
    auto counter = std::make_shared<JobHandle::Counter>();
    counter->value.store(1, std::memory_order_release);
    counter->owner = this;

    auto* job = new Job{};
    job->func = std::move(func);
    job->counter = counter;

    // Keep one pending-dependency slot while registration is in progress.
    // A dependency may finish while we register later dependencies; without
    // this guard it could enqueue and run `job` before its full DAG is known.
    job->pending_deps.store(1, std::memory_order_release);
    total_jobs_.fetch_add(1, std::memory_order_acq_rel);

    // Register unfinished dependencies while holding each Counter lock. The
    // value check and waiter insertion must be one critical section: a prior
    // is_done() check could race with on_counter_zero() and strand the job.
    for (auto& dep : dependencies) {
        if (!dep.counter_) continue;

        std::lock_guard<std::mutex> lk(dep.counter_->mtx);
        if (dep.counter_->value.load(std::memory_order_acquire) == 0) {
            continue;
        }

        // Safe raw pointer: `job` retains its registration guard until all
        // dependency slots are installed, and cannot run/delete itself until
        // every registered Counter reaches zero.
        job->pending_deps.fetch_add(1, std::memory_order_acq_rel);
        dep.counter_->waiters.push_back(job);
    }

    // Drop the registration guard. If no dependency remains, this is the
    // single transition that makes the job runnable.
    if (job->pending_deps.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        enqueue(job);
    }

    return JobHandle{counter};
}

JobHandle JobSystemP2::parallel_for(int32_t count, JobFunc func,
                                    int32_t batch_size) {
    if (count <= 0) return JobHandle{};

    // Clamp batch_size to [1, count] so we always make progress.
    if (batch_size < 1) batch_size = 1;
    if (batch_size > count) batch_size = count;

    const int32_t num_tiles = (count + batch_size - 1) / batch_size;

    auto counter = std::make_shared<JobHandle::Counter>();
    counter->value.store(num_tiles, std::memory_order_release);
    counter->owner = this;
    total_jobs_.fetch_add(num_tiles, std::memory_order_acq_rel);

    // Pre-capture `func` in a shared_ptr ONCE, outside the loop. Each
    // tile job captures this shared_ptr by value (cheap copy) and calls
    // (*shared_func)(thread, i) for each index in [begin, end).
    //
    // We must NOT move `func` inside the loop — the first iteration would
    // steal it and leave subsequent tiles with an empty std::function.
    //
    // We don't reuse submit() here because submit() allocates a per-job
    // Counter, which would be wasteful (we already have one shared
    // Counter for the whole parallel_for).
    auto shared_func = std::make_shared<JobFunc>(std::move(func));
    for (int32_t t = 0; t < num_tiles; ++t) {
        const int32_t begin = t * batch_size;
        const int32_t end = std::min(begin + batch_size, count);

        auto* job = new Job{};
        job->func = [shared_func, begin, end](int32_t thread_idx, int32_t) {
            for (int32_t i = begin; i < end; ++i) {
                (*shared_func)(thread_idx, i);
            }
        };
        job->counter = counter;
        job->pending_deps.store(0, std::memory_order_release);
        enqueue(job);
    }

    return JobHandle{counter};
}

int32_t JobSystemP2::worker_count() const {
    return worker_count_;
}

// ---------------------------------------------------------------------------
// JobSystemP2 private: worker loop + queue ops
// ---------------------------------------------------------------------------

void JobSystemP2::worker_loop(int32_t thread_index) {
    g_current_job_system = this;
    g_current_worker_index = thread_index;
    WorkerQueue& my_queue = *queues_[thread_index];

    while (true) {
        Job* job = nullptr;

        // 1. Try own queue (LIFO pop back — cache friendly).
        if (try_pop_own(my_queue, &job)) {
            run_job(thread_index, job);
            continue;
        }

        // 2. Try stealing from other workers (FIFO pop front).
        for (int32_t i = 1; i < worker_count_; ++i) {
            int32_t victim = (thread_index + i) % worker_count_;
            if (try_steal(*queues_[victim], &job)) {
                run_job(thread_index, job);
                job = nullptr;
                break;
            }
        }
        if (job != nullptr) continue;

        // 3. Nothing to do. Wait on the global cv until submit() or
        //    on_counter_zero() wakes us, OR stopping_ is set.
        std::unique_lock<std::mutex> lk(global_mtx_);
        global_cv_.wait_for(lk, std::chrono::milliseconds(1), [this] {
            return stopping_.load(std::memory_order_acquire) ||
                   total_jobs_.load(std::memory_order_acquire) > 0;
        });
        if (stopping_.load(std::memory_order_acquire)) {
            // Drain any remaining jobs before exiting (graceful shutdown).
            if (try_pop_own(my_queue, &job)) {
                lk.unlock();
                run_job(thread_index, job);
                continue;
            }
            for (int32_t i = 1; i < worker_count_; ++i) {
                int32_t victim = (thread_index + i) % worker_count_;
                if (try_steal(*queues_[victim], &job)) {
                    lk.unlock();
                    run_job(thread_index, job);
                    goto next_iter;
                }
            }
            break;  // No more work + stopping_ -> exit.
        }
        next_iter:;
    }

    g_current_worker_index = -1;
    g_current_job_system = nullptr;
}

void JobSystemP2::enqueue(Job* job) {
    // Round-robin: spread independent jobs across workers. We skip the
    // calling worker if the caller is a worker thread (so the caller's
    // LIFO doesn't get buried under stolen work) — but in practice the
    // main thread submits most jobs, so simple round-robin is fine.
    const int32_t idx = next_queue_.fetch_add(1, std::memory_order_acq_rel)
                        % worker_count_;
    {
        std::lock_guard<std::mutex> lk(queues_[idx]->mtx);
        queues_[idx]->jobs.push_back(job);
    }
    global_cv_.notify_one();
}

bool JobSystemP2::try_pop_own(WorkerQueue& q, Job** out) {
    std::lock_guard<std::mutex> lk(q.mtx);
    if (q.jobs.empty()) return false;
    // LIFO: owner pops from the back. This keeps the working set hot in
    // L1 cache (the most recently pushed job is most likely to share
    // data with the current execution context).
    *out = q.jobs.back();
    q.jobs.pop_back();
    return true;
}

bool JobSystemP2::try_steal(WorkerQueue& q, Job** out) {
    std::lock_guard<std::mutex> lk(q.mtx);
    if (q.jobs.empty()) return false;
    // FIFO: stealer pops from the front. Stealing from the front reduces
    // contention with the owner (which pops from the back), and gives
    // the stealer the oldest enqueued job (most likely to be ready).
    *out = q.jobs.front();
    q.jobs.pop_front();
    return true;
}

bool JobSystemP2::try_run_one(int32_t worker_index) {
    if (worker_index < 0 || worker_index >= worker_count_) return false;

    Job* job = nullptr;
    if (try_pop_own(*queues_[worker_index], &job)) {
        run_job(worker_index, job);
        return true;
    }

    for (int32_t i = 1; i < worker_count_; ++i) {
        const int32_t victim = (worker_index + i) % worker_count_;
        if (try_steal(*queues_[victim], &job)) {
            run_job(worker_index, job);
            return true;
        }
    }
    return false;
}

void JobSystemP2::wait_for_counter(
    const std::shared_ptr<JobHandle::Counter>& counter) {
    if (!counter || counter->value.load(std::memory_order_acquire) == 0) {
        return;
    }

    if (g_current_job_system == this) {
        // Nested waits must actively execute available work. In particular,
        // a one-worker pool would otherwise wait forever for child tiles that
        // only that same worker can dequeue.
        while (counter->value.load(std::memory_order_acquire) != 0) {
            if (!try_run_one(g_current_worker_index)) {
                std::this_thread::yield();
            }
        }
        return;
    }

    // Main and foreign-pool threads block rather than reentering this pool.
    std::unique_lock<std::mutex> lk(counter->mtx);
    counter->cv.wait(lk, [&counter] {
        return counter->value.load(std::memory_order_acquire) == 0;
    });
}

void JobSystemP2::run_job(int32_t thread_index, Job* job) {
    // Execute the user function. Exceptions are caught and swallowed —
    // propagating across thread boundaries is undefined in std::function.
    // A future revision can route these to snt::core::Logger.
    try {
        job->func(thread_index, 0);
    } catch (...) {
        // Swallow; the Counter is still decremented below so waiters
        // don't deadlock.
    }

    // Atomically decrement this job's counter. If we are the last
    // reference (value goes 1 -> 0), wake up any dependent jobs and
    // any thread blocked in JobHandle::wait().
    const int32_t prev = job->counter->value.fetch_sub(
        1, std::memory_order_acq_rel);
    if (prev == 1) {
        on_counter_zero(job->counter.get());
    }

    total_jobs_.fetch_sub(1, std::memory_order_acq_rel);

    delete job;
}

void JobSystemP2::on_counter_zero(JobHandle::Counter* c) {
    // Wake non-worker waiters blocked in JobHandle::wait().
    {
        std::lock_guard<std::mutex> lk(c->mtx);
        // Move waiters out so we can enqueue them without holding the
        // Counter lock (enqueue() takes a queue lock — lock ordering:
        // always queue lock first, never Counter lock inside queue lock).
        std::vector<Job*> to_enqueue;
        to_enqueue.swap(c->waiters);
        // Release the lock by ending scope after swap.
        // We need to decrement pending_deps OUTSIDE the queue lock too,
        // because pending_deps is atomic and doesn't need the Counter lock.
        // But we still hold c->mtx here. Decrement + enqueue while holding
        // c->mtx is fine — enqueue takes queue.mtx, which is a different
        // lock and we always acquire Counter.mtx before queue.mtx in
        // submit()'s dep registration, so the ordering is consistent.
        for (Job* waiter : to_enqueue) {
            const int32_t prev_deps = waiter->pending_deps.fetch_sub(
                1, std::memory_order_acq_rel);
            if (prev_deps == 1) {
                // Last dep satisfied — enqueue the waiter. Still holding
                // c->mtx here, which is consistent with submit()'s lock
                // ordering (Counter.mtx -> queue.mtx).
                enqueue(waiter);
            }
        }
    }
    c->cv.notify_all();
    global_cv_.notify_all();
}

// ---------------------------------------------------------------------------
// Global default instance + override
// ---------------------------------------------------------------------------

JobSystem& default_job_system() {
    // Built-in fallback: a P1 stub, lazily initialized. Only used if
    // nobody has called set_default_job_system() (e.g. in unit tests
    // that don't construct a JobSystemP2).
    static JobSystem fallback;
    static bool initialized = false;
    if (!initialized) {
        fallback.init();
        initialized = true;
    }
    if (g_default_override != nullptr) {
        return *g_default_override;
    }
    return fallback;
}

void set_default_job_system(JobSystem* new_default) {
    g_default_override = new_default;
}

}  // namespace snt::core
