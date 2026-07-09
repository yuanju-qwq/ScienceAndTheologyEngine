// Engine-wide Clock abstraction.
//
// Design goals:
//   - Decouple time-reading from std::chrono so systems (animation, physics,
//     input, logger) depend on an interface, not a concrete clock. This
//     enables:
//       * Test mocks: ManualClock advances only when the test calls tick().
//       * Future time scaling: a wrapper Clock can apply pause / slow-mo /
//         fast-forward without touching call sites.
//   - Stable monotonic time: the engine never uses wall-clock time for
//     gameplay decisions (steady_clock semantics). Wall-clock is only used
//     for log timestamps, which keep using system_clock internally.
//   - Cheap: virtual call per frame is negligible; systems that read time
//     every frame (Engine::run) cache `now()` once per frame.
//
// Usage:
//   // Production:
//   RealClock clock;
//   auto t0 = clock.now();
//   ... work ...
//   auto dt = clock.delta_since(t0);
//
//   // Tests:
//   ManualClock clock;
//   clock.advance(DurationMs(16.0f));  // simulate one frame
//   EXPECT_NEAR(clock.delta_since(t0).count(), 16.0f, 0.001f);
//
// Thread-safety: RealClock is thread-safe (steady_clock is). ManualClock is
// NOT thread-safe (intended for single-threaded tests).

#pragma once

#include <chrono>

namespace snt::core {

// Duration type used engine-wide. Float milliseconds so dt arithmetic
// doesn't overflow integer ranges and stays friendly with gameplay math.
using DurationMs = std::chrono::duration<float, std::milli>;

// Time points keep double precision so long-running machines do not lose
// sub-frame deltas when steady_clock's epoch value grows large. Public deltas
// stay as float milliseconds via DurationMs.
using TimePointDurationMs = std::chrono::duration<double, std::milli>;
using TimePoint = std::chrono::time_point<std::chrono::steady_clock, TimePointDurationMs>;

// ---------------------------------------------------------------------------
// IClock: interface used by all time-reading systems.
// ---------------------------------------------------------------------------
class IClock {
public:
    virtual ~IClock() = default;

    // Current engine time. Stable + monotonic; never goes backward.
    virtual TimePoint now() const = 0;

    // Convenience: elapsed time since `earlier`. Returns a non-negative
    // duration; if `earlier` is somehow after `now()` (shouldn't happen
    // for a monotonic clock), returns zero.
    DurationMs delta_since(TimePoint earlier) const {
        const auto n = now();
        return n > earlier ? std::chrono::duration_cast<DurationMs>(n - earlier)
                           : DurationMs::zero();
    }
};

// ---------------------------------------------------------------------------
// RealClock: production clock backed by std::chrono::steady_clock.
// ---------------------------------------------------------------------------
// steady_clock is guaranteed monotonic + non-decreasing, making it safe
// for gameplay timing (delta time, animation, physics integration).
class RealClock : public IClock {
public:
    TimePoint now() const override {
        return std::chrono::time_point_cast<TimePointDurationMs>(
            std::chrono::steady_clock::now());
    }
};

// ---------------------------------------------------------------------------
// ManualClock: test-only clock with explicit time advancement.
// ---------------------------------------------------------------------------
// `advance()` moves the internal epoch forward; `now()` returns the
// accumulated time. Lets unit tests deterministically simulate frame
// deltas without sleeping.
class ManualClock : public IClock {
public:
    TimePoint now() const override { return current_; }

    // Advance the clock by `dt`. Negative deltas are clamped to zero
    // (monotonic guarantee).
    void advance(DurationMs dt) {
        if (dt > DurationMs::zero()) {
            current_ += dt;
        }
    }

    // Convenience: advance by a whole number of milliseconds.
    void advance_ms(float ms) { advance(DurationMs(ms)); }

private:
    TimePoint current_{};
};

}  // namespace snt::core
