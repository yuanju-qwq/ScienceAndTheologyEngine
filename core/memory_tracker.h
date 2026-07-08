// CPU-side memory statistics + allocator tracking.
//
// Design goals:
//   - Global visibility: overload operator new / delete so EVERY allocation
//     in the process (engine + third-party libs like SDL/VMA/EnTT) is
//     counted. VMA already tracks GPU memory; this closes the CPU gap.
//   - Cheap: a handful of std::atomic counters updated on every alloc/free.
//     The atomic rmw cost is negligible compared to malloc itself, so this
//     is safe to leave enabled in Release.
//   - No stack traces by default (would dwarf the cost). For "who allocated
//     100MB" forensics, add a captured-stack mode later behind a flag.
//   - ScopeAllocator: RAII delta tracker for slicing a subsystem or frame.
//     Pairs with the global counters — global gives the total, ScopeAllocator
//     gives the slice.
//
// Usage:
//   // Global stats (anywhere):
//   snt::core::MemoryTracker::instance().log_stats();
//
//   // Per-frame / per-subsystem slice:
//   {
//       snt::core::ScopeAllocator scope("render_frame");
//       ... work ...
//       // dtor logs: "[mem] render_frame: +12345B (peak within scope)"
//   }
//
// Thread-safety: counters are std::atomic; ScopeAllocator is per-instance
// and intended to be used from a single thread (the one that owns it).

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace snt::core {

// ---------------------------------------------------------------------------
// MemoryTracker: process-wide singleton counting every new/delete.
// ---------------------------------------------------------------------------
// Updated by the global operator new / delete overloads in memory_tracker.cpp.
// All mutators are lock-free atomics so the tracker is safe to call from
// any thread, including inside third-party libs that allocate internally.
class MemoryTracker {
public:
    // Access the global instance.
    static MemoryTracker& instance();

    // Recorded by overloaded operator new / delete. Exposed publicly so
    // custom allocators (e.g. a future pool allocator) can opt in.
    void record_alloc(std::size_t bytes);
    void record_dealloc(std::size_t bytes);

    // Snapshot of current counters. Safe to call concurrently with allocs.
    struct Stats {
        std::size_t current_bytes;     // live bytes (alloc - dealloc)
        std::size_t peak_bytes;        // high-water mark since startup
        std::size_t alloc_count;       // total allocations since startup
        std::size_t dealloc_count;     // total deallocations since startup
    };
    Stats stats() const;

    // Convenience: log stats via SNT_LOG_INFO. Channel is "mem".
    void log_stats() const;

private:
    MemoryTracker();
    ~MemoryTracker();
    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;

    std::atomic<std::size_t> current_bytes_{0};
    std::atomic<std::size_t> peak_bytes_{0};
    std::atomic<std::size_t> alloc_count_{0};
    std::atomic<std::size_t> dealloc_count_{0};
};

// ---------------------------------------------------------------------------
// ScopeAllocator: RAII delta tracker for a code region.
// ---------------------------------------------------------------------------
// Captures current_bytes on construction, logs the delta on destruction.
// Use to answer "how much did rendering this frame allocate?" without
// disturbing the global counters.
//
// Note: ScopeAllocator itself only measures deltas in the GLOBAL tracker.
// If you need per-thread or per-category isolation, extend the tracker
// later — the API surface here is intentionally minimal.
class ScopeAllocator {
public:
    // `label` is logged on destruction; keep it short (e.g. "render_frame").
    explicit ScopeAllocator(const char* label);
    ~ScopeAllocator();

    ScopeAllocator(const ScopeAllocator&) = delete;
    ScopeAllocator& operator=(const ScopeAllocator&) = delete;

    // Live delta since construction (without destroying the scope).
    std::size_t delta_bytes() const;

private:
    const char*    label_;
    std::size_t    start_bytes_;
};

}  // namespace snt::core
