// CPU-side memory statistics — implementation.
//
// Wires MemoryTracker to the global operator new / delete overloads.
// Every allocation in the process (engine + third-party libs) flows through
// these overloads, so the counters reflect true process-wide CPU memory.
//
// Reentry note: the overloads call malloc / free directly (never operator
// new), and MemoryTracker itself uses only std::atomic (no allocations),
// so there is no risk of infinite recursion.

#define SNT_LOG_CHANNEL "mem"
#include "core/log.h"
#include "core/memory_tracker.h"

#include <cstdlib>     // malloc / free
#include <new>         // std::align_val_t, std::nothrow_t

#if defined(_MSC_VER)
#  include <malloc.h>  // _aligned_malloc / _aligned_free
#endif

namespace snt::core {

// ---------------------------------------------------------------------------
// Platform abstraction for aligned allocation.
// ---------------------------------------------------------------------------
// MSVC does not provide std::aligned_alloc (C11 std::aligned_alloc is not
// shipped with MSVC's <cstdlib>). Use _aligned_malloc / _aligned_free on
// Windows instead. Note the argument order differs: _aligned_malloc takes
// (size, alignment) while std::aligned_alloc takes (alignment, size).
// Pointers from _aligned_malloc MUST be freed by _aligned_free (not free).
inline void* snt_aligned_alloc(std::size_t size, std::size_t alignment) {
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
    // std::aligned_alloc requires size to be a multiple of alignment on
    // some platforms; round up to be safe.
    const std::size_t sz = (size + alignment - 1) & ~(alignment - 1);
    return std::aligned_alloc(alignment, sz);
#endif
}

inline void snt_aligned_free(void* p) {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

}  // namespace snt::core (closing platform-abstraction scope)

// snt_aligned_alloc/free are used by the global operator new/delete
// overloads below. They live in the snt::core namespace but are accessed
// without qualification since the overloads are at global scope.
using snt::core::snt_aligned_alloc;
using snt::core::snt_aligned_free;

// ===========================================================================
// MemoryTracker implementation
// ===========================================================================
namespace snt::core {
MemoryTracker::MemoryTracker() = default;
MemoryTracker::~MemoryTracker() = default;

MemoryTracker& MemoryTracker::instance() {
    static MemoryTracker inst;
    return inst;
}

void MemoryTracker::record_alloc(std::size_t bytes) {
    // Update current + alloc_count first; then refresh peak via CAS.
    const std::size_t now = current_bytes_.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    alloc_count_.fetch_add(1, std::memory_order_relaxed);

    std::size_t peak = peak_bytes_.load(std::memory_order_relaxed);
    while (now > peak &&
           !peak_bytes_.compare_exchange_weak(peak, now,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {
        // peak has been refreshed by CAS failure; loop until we either
        // raise it or observe a value >= now.
    }
}

void MemoryTracker::record_dealloc(std::size_t bytes) {
    current_bytes_.fetch_sub(bytes, std::memory_order_relaxed);
    dealloc_count_.fetch_add(1, std::memory_order_relaxed);
}

MemoryTracker::Stats MemoryTracker::stats() const {
    return Stats{
        .current_bytes = current_bytes_.load(std::memory_order_relaxed),
        .peak_bytes    = peak_bytes_.load(std::memory_order_relaxed),
        .alloc_count   = alloc_count_.load(std::memory_order_relaxed),
        .dealloc_count = dealloc_count_.load(std::memory_order_relaxed),
    };
}

void MemoryTracker::log_stats() const {
    const auto s = stats();
    SNT_LOG_INFO("CPU memory: current=%.2f MB, peak=%.2f MB, allocs=%zu, deallocs=%zu",
                 static_cast<double>(s.current_bytes) / (1024.0 * 1024.0),
                 static_cast<double>(s.peak_bytes)    / (1024.0 * 1024.0),
                 s.alloc_count,
                 s.dealloc_count);
}

// ---------------------------------------------------------------------------
// ScopeAllocator
// ---------------------------------------------------------------------------
ScopeAllocator::ScopeAllocator(const char* label)
    : label_(label),
      start_bytes_(MemoryTracker::instance().stats().current_bytes) {}

ScopeAllocator::~ScopeAllocator() {
    const std::size_t end = MemoryTracker::instance().stats().current_bytes;
    // Underflow guard: deallocations from outside the scope can make end <
    // start; report the raw signed delta so the user sees the truth.
    const long long delta = static_cast<long long>(end) - static_cast<long long>(start_bytes_);
    SNT_LOG_INFO("[scope:%s] delta = %+lld B (end=%zu B)",
                 label_, delta, end);
}

std::size_t ScopeAllocator::delta_bytes() const {
    const std::size_t end = MemoryTracker::instance().stats().current_bytes;
    return end > start_bytes_ ? end - start_bytes_ : 0;
}

}  // namespace snt::core

// ---------------------------------------------------------------------------
// Global operator new / delete overloads.
// ---------------------------------------------------------------------------
// Forward every form (plain, array, aligned, nothrow) to malloc/free and
// record the size. nothrow variants return nullptr on failure (matching
// std::nothrow semantics); the throwing variants let malloc's nullptr
// propagate as std::bad_alloc by throwing explicitly.
// ---------------------------------------------------------------------------
void* operator new(std::size_t n) {
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc();
    snt::core::MemoryTracker::instance().record_alloc(n);
    return p;
}

void* operator new[](std::size_t n) {
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc();
    snt::core::MemoryTracker::instance().record_alloc(n);
    return p;
}

void* operator new(std::size_t n, std::align_val_t align) {
    // aligned_alloc requires the size to be a multiple of alignment on
    // some platforms; round up to be safe.
    const std::size_t a = static_cast<std::size_t>(align);
    void* p = snt_aligned_alloc(n, a);
    if (!p) throw std::bad_alloc();
    snt::core::MemoryTracker::instance().record_alloc(n);
    return p;
}

void* operator new[](std::size_t n, std::align_val_t align) {
    const std::size_t a = static_cast<std::size_t>(align);
    void* p = snt_aligned_alloc(n, a);
    if (!p) throw std::bad_alloc();
    snt::core::MemoryTracker::instance().record_alloc(n);
    return p;
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    void* p = std::malloc(n);
    if (p) snt::core::MemoryTracker::instance().record_alloc(n);
    return p;
}

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    void* p = std::malloc(n);
    if (p) snt::core::MemoryTracker::instance().record_alloc(n);
    return p;
}

void* operator new(std::size_t n, std::align_val_t align, const std::nothrow_t&) noexcept {
    const std::size_t a = static_cast<std::size_t>(align);
    void* p = snt_aligned_alloc(n, a);
    if (p) snt::core::MemoryTracker::instance().record_alloc(n);
    return p;
}

void* operator new[](std::size_t n, std::align_val_t align, const std::nothrow_t&) noexcept {
    const std::size_t a = static_cast<std::size_t>(align);
    void* p = snt_aligned_alloc(n, a);
    if (p) snt::core::MemoryTracker::instance().record_alloc(n);
    return p;
}

// ---------------------------------------------------------------------------
// operator delete: the unsized overloads pass 0 to record_dealloc (we
// don't know the original allocation size); the sized overloads pass the
// size for accurate current_bytes accounting. Both bump dealloc_count.
// ---------------------------------------------------------------------------
void operator delete(void* p) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(0);
    std::free(p);
}

void operator delete[](void* p) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(0);
    std::free(p);
}

void operator delete(void* p, std::size_t n) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(n);
    std::free(p);
}

void operator delete[](void* p, std::size_t n) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(n);
    std::free(p);
}

void operator delete(void* p, std::align_val_t) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(0);
    snt_aligned_free(p);
}

void operator delete[](void* p, std::align_val_t) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(0);
    snt_aligned_free(p);
}

void operator delete(void* p, std::size_t n, std::align_val_t) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(n);
    snt_aligned_free(p);
}

void operator delete[](void* p, std::size_t n, std::align_val_t) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(n);
    snt_aligned_free(p);
}

// Matching nothrow placement-delete (called if the corresponding nothrow
// new returns nullptr — standard requires them to exist).
void operator delete(void* p, const std::nothrow_t&) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(0);
    std::free(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(0);
    std::free(p);
}

void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(0);
    snt_aligned_free(p);
}

void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    if (!p) return;
    snt::core::MemoryTracker::instance().record_dealloc(0);
    snt_aligned_free(p);
}
