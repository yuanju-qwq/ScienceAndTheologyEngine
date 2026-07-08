// Job System — Counter-style API inspired by Naughty Dog's GDC 2015 talk.
//
// Design goals:
//   - Express task dependency graphs (System B waits on System A).
//   - Batch parallel-for for ECS system scheduling over component ranges.
//   - P1: stub serial implementation (single-threaded, validates API design).
//   - P2: real thread pool with work-stealing queues.
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
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace snt::core {

// Opaque handle to a submitted job. nullptr means "no job" / already complete.
// Internally wraps an atomic counter shared_ptr.
class JobHandle {
public:
    JobHandle() = default;

    // Returns true if the job (and all its dependencies) has completed.
    bool is_done() const;

    // Block the calling thread until this job completes.
    void wait() const;

private:
    friend class JobSystem;
    template <typename T> friend class Future;
    struct Counter {
        std::atomic<int32_t> value{0};
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

// Global default job system instance (defined in job_system.cpp).
// Main loop and ECS systems use this by default.
JobSystem& default_job_system();

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
