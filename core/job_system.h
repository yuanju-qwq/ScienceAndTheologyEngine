// Job System — Counter-style API inspired by Naughty Dog's GDC 2015 talk.
//
// Design goals:
//   - Express task dependency graphs (System B waits on System A).
//   - Batch parallel-for for ECS system scheduling over component ranges.
//   - P1: stub serial implementation (single-threaded, validates API design).
//   - P2: real thread pool with per-worker deque + work-stealing, dependency
//         tracking via Counter::waiters, graceful shutdown.
//
// Core concept:
//   A "Counter" is a shared atomic counter. submit() increments it, the
//   worker thread decrements it on completion. wait(counter) blocks until
//   the counter reaches zero. Dependencies are expressed by chaining:
//     counterA = submit(jobA);
//     counterB = submit(jobB, {counterA});  // B starts only after A done.
//
// Usage example:
//   JobSystem js;
//   auto c1 = js.submit([]{ /* tick physics */ });
//   auto c2 = js.submit([]{ /* tick AI */ });
//   js.wait(c1);
//   js.wait(c2);

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace snt::core {

// Forward declarations (definitions in job_system.cpp).
class JobSystem;
class JobSystemP2;
struct Job;

// Opaque handle to a submitted job. nullptr means "no job" / already complete.
// Internally wraps an atomic counter shared_ptr.
class JobHandle {
public:
    JobHandle() = default;

    // Returns true if the job (and all its dependencies) has completed.
    bool is_done() const;

    // Block the calling thread until this job completes.
    // - Worker threads: help other workers by stealing + running jobs
    //   while waiting (Naughty Dog "work as wait" style).
    // - Main thread: condvar-block to avoid reentrancy in render/ECS.
    void wait() const;

private:
    friend class JobSystem;
    friend class JobSystemP2;
    friend struct Job;  // Job needs to reference Counter type + fields
    template <typename T> friend class Future;

    // Counter shared between the submitted Job, its handle, and any
    // dependents waiting on it. value>0 means "still running or pending".
    //
    // waiters: jobs whose `pending_deps` was bumped because they listed
    // this counter as a dependency. When value hits 0, each waiter's
    // pending_deps is decremented; the one that reaches 0 first gets
    // enqueued by the JobSystem.
    struct Counter {
        std::atomic<int32_t> value{0};
        // P2 fields below are unused by the P1 stub; they live here so the
        // Counter layout is identical across implementations (avoids ABI
        // friction when default_job_system() swaps P1 <-> P2 at runtime).
        std::mutex mtx;
        std::condition_variable cv;
        std::vector<Job*> waiters;  // raw pointers; owned by JobSystem
    };
    std::shared_ptr<Counter> counter_;
    JobHandle(std::shared_ptr<Counter> c) : counter_(std::move(c)) {}
};

// Future<T>: lightweight typed result wrapper around a JobHandle.
//
// Design:
//   - Result stored in shared_ptr<T> (heap-allocated, shared ownership).
//   - No .then() chaining (keeps templates shallow, compile times low).
//   - Use Case: async resource loading; ECS scheduling uses JobHandle directly.
//
// Usage:
//   Future<MeshData> fut = js.submit_future([]{ return load_mesh(); });
//   MeshData mesh = fut.get();  // blocks until done
template <typename T>
class Future {
public:
    Future() = default;

    // Wait for the job to complete and return the result.
    T get() const {
        wait();
        return *result_;
    }

    // Block until the underlying job completes.
    void wait() const { handle_.wait(); }

    // Returns true if the job has completed (result is ready).
    bool is_ready() const { return handle_.is_done(); }

private:
    friend class JobSystem;
    Future(JobHandle h, std::shared_ptr<T> result)
        : handle_(std::move(h)), result_(std::move(result)) {}

    JobHandle handle_;
    std::shared_ptr<T> result_;
};

// Job function: takes the worker thread index (0..N-1) and a tile index
// (for parallel_for, the index of the tile being processed).
using JobFunc = std::function<void(int32_t thread_index, int32_t tile_index)>;

// ---------------------------------------------------------------------------
// Internal Job struct (P2).
//
// A Job is the unit of work scheduled by JobSystemP2. It is owned by the
// JobSystemP2 instance until it has executed, then deleted. Each Job has:
//   - func:           the user-supplied lambda
//   - counter:        shared_ptr to its Counter; value goes 1 -> 0 on done
//   - pending_deps:   number of dependency Counters not yet at 0; the job
//                     is only enqueued when this hits 0
//   - is_parallel_tile: marks parallel_for sub-jobs (no standalone counter
//                     allocation; instead the parent's counter is used and
//                     decremented per tile)
//
// Lifetime: created by submit() / parallel_for(); destroyed after the
// worker thread finishes running `func`. Raw Job* stored in Counter::waiters
// is guaranteed valid because the dependent Job cannot run (and be deleted)
// until its pending_deps reaches 0, which requires the waiter Counter to
// hit 0 first — so the Job holding the Counter alive is still alive.
struct Job {
    JobFunc func;
    std::shared_ptr<JobHandle::Counter> counter;
    std::atomic<int32_t> pending_deps{0};
};

// Job System interface.
// P1 implementation is serial (runs on the calling thread); the API is
// designed to allow a drop-in multi-threaded replacement in P2.
class JobSystem {
public:
    JobSystem() = default;
    virtual ~JobSystem() = default;

    // Non-copyable, non-movable (shared state across threads).
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Initialize the system with `worker_count` threads.
    // P1 stub: worker_count is ignored (always runs on caller thread).
    virtual void init(int32_t worker_count = 0);

    // Shut down the system, waiting for all pending jobs.
    virtual void shutdown();

    // Submit a single job. The returned handle can be waited on or used
    // as a dependency for other jobs.
    // dependencies: jobs that must complete before this one starts.
    virtual JobHandle submit(JobFunc func,
                             std::span<JobHandle> dependencies = {});

    // Parallel-for: split [0, count) into `count` tiles and run `func`
    // for each tile. Returns a handle that completes when all tiles finish.
    //
    // This is the primary entry point for ECS system scheduling:
    //   js.parallel_for(entities.size(), [&](int, int i){
    //       tick_entity(entities[i]);
    //   });
    virtual JobHandle parallel_for(int32_t count, JobFunc func,
                                   int32_t batch_size = 1);

    // Submit a job that returns a value, wrapped in Future<T>.
    // Use for async resource loading (e.g. load_mesh, load_texture).
    // Dependencies: jobs that must complete before this one starts.
    //
    // Usage:
    //   Future<Mesh> m = js.submit_future([]{ return load_mesh(); });
    //   Future<Tex>  t = js.submit_future([]{ return load_tex(); });
    //   Mesh mesh = m.get();  // blocks
    //   Tex  tex  = t.get();
    template <typename T>
    Future<T> submit_future(std::function<T()> func,
                            std::span<JobHandle> dependencies = {});

    // Number of worker threads (P1 stub: always 1, the calling thread).
    virtual int32_t worker_count() const;

private:
    int32_t worker_count_ = 1;
};

// ---------------------------------------------------------------------------
// JobSystemP2: real multi-threaded implementation (P2).
//
// Architecture:
//   - N worker threads, each with a local deque (mutex-protected for now;
//     a lock-free Chase-Lev deque is a future P3 optimization).
//   - Owner of a deque pushes/pops from the back (LIFO, cache-friendly);
//     stealers pop from the front (FIFO).
//   - Dependencies: when submit() is called with deps that are not yet
//     done, the Job is parked (not enqueued). pending_deps counts how
//     many deps remain; each dep's Counter::waiters gets a raw Job*.
//     When a Counter hits 0, every waiter's pending_deps is decremented
//     under the Counter's mutex; waiters that reach 0 are enqueued.
//   - wait(): worker threads steal + run jobs while waiting (work-as-wait);
//     the main thread (identified by thread id) condvar-blocks to avoid
//     reentrancy into render/ECS code.
//   - shutdown(): graceful — set stopping_, wake all workers, drain
//     pending + running jobs, join threads.
class JobSystemP2 : public JobSystem {
public:
    JobSystemP2() = default;
    ~JobSystemP2() override;

    void init(int32_t worker_count = 0) override;
    void shutdown() override;

    JobHandle submit(JobFunc func,
                     std::span<JobHandle> dependencies = {}) override;
    JobHandle parallel_for(int32_t count, JobFunc func,
                           int32_t batch_size = 1) override;

    int32_t worker_count() const override;

    // True if the calling thread is the one that called init() (i.e. the
    // main / engine thread). JobHandle::wait() uses this to decide between
    // condvar-blocking (main thread) and work-as-wait (worker threads).
    bool is_main_thread() const {
        return std::this_thread::get_id() == main_thread_id_;
    }

private:
    // Per-worker state: a deque + mutex. Owner pushes/pops back (LIFO);
    // stealers try_pop front. Work-stealing lets an idle worker help a
    // busy one, balancing load across the pool.
    struct WorkerQueue {
        std::deque<Job*> jobs;
        std::mutex mtx;
    };

    void worker_loop(int32_t thread_index);
    void enqueue(Job* job);                 // round-robin onto workers
    bool try_pop_own(WorkerQueue& q, Job** out);  // LIFO pop back (owner)
    bool try_steal(WorkerQueue& q, Job** out);    // FIFO pop front (stealer)
    void run_job(int32_t thread_index, Job* job);
    void on_counter_zero(JobHandle::Counter* c);  // wake dependents + waiters

    std::vector<std::unique_ptr<WorkerQueue>> queues_;
    std::vector<std::thread> threads_;
    std::atomic<int32_t> next_queue_{0};    // round-robin submit cursor
    std::atomic<bool> stopping_{false};

    // Shared wake-up cv: workers wait on this when their own queue is
    // empty AND stealing failed. submit() / on_counter_zero() notify_all.
    std::mutex global_mtx_;
    std::condition_variable global_cv_;
    std::atomic<int32_t> total_jobs_{0};    // outstanding (running+pending)

    int32_t worker_count_ = 0;
    std::thread::id main_thread_id_;        // main thread = caller of init()
};

// Global default job system instance (defined in job_system.cpp).
// Main loop and ECS systems use this by default.
JobSystem& default_job_system();

// Override the global default job system. Engine calls this at init() to
// swap the P1 stub for a real JobSystemP2; tests can also swap in their
// own instance. Pass nullptr to revert to the built-in default.
// `new_default` must outlive all callers of default_job_system().
void set_default_job_system(JobSystem* new_default);

// ---------------------------------------------------------------------------
// Template method implementations (must be in header for instantiation).
// ---------------------------------------------------------------------------

template <typename T>
Future<T> JobSystem::submit_future(std::function<T()> func,
                                   std::span<JobHandle> dependencies) {
    // Result stored in shared_ptr so Future copies share it.
    auto result = std::make_shared<T>();
    // Wrap the returning func into a void JobFunc that writes the result.
    JobFunc wrapped = [result, f = std::move(func)](int32_t, int32_t) {
        *result = f();
    };
    JobHandle handle = submit(std::move(wrapped), dependencies);
    return Future<T>(std::move(handle), std::move(result));
}

}  // namespace snt::core
