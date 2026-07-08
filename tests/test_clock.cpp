// Tests for the Clock abstraction.
//
// Covers:
//   - RealClock returns increasing time points (monotonic)
//   - ManualClock advances only when ticked
//   - delta_since returns correct elapsed time
//   - ManualClock clamps negative advances to zero

#include "core/clock.h"

#include <gtest/gtest.h>
#include <thread>

using snt::core::IClock;
using snt::core::RealClock;
using snt::core::ManualClock;
using snt::core::DurationMs;
using snt::core::TimePoint;

TEST(RealClock, MonotonicNonDecreasing) {
    RealClock clock;
    const auto t0 = clock.now();
    const auto t1 = clock.now();
    EXPECT_GE(t1, t0);
}

TEST(RealClock, DeltaSinceMeasuresElapsed) {
    RealClock clock;
    const auto t0 = clock.now();
    // Sleep ~5ms; steady_clock resolution on most platforms is <= 1ms.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto dt = clock.delta_since(t0);
    EXPECT_GE(dt.count(), 4.0f);  // allow some scheduling slack
    EXPECT_LT(dt.count(), 100.0f);  // upper bound sanity check
}

TEST(ManualClock, StartsAtZero) {
    ManualClock clock;
    // ManualClock's epoch is implementation-defined but must be stable.
    const auto t0 = clock.now();
    const auto t1 = clock.now();
    EXPECT_EQ(t0, t1);  // No advance -> same time point
}

TEST(ManualClock, AdvanceIncreasesTime) {
    ManualClock clock;
    const auto t0 = clock.now();
    clock.advance_ms(16.0f);
    const auto t1 = clock.now();
    const auto dt = (t1 - t0).count();
    EXPECT_NEAR(dt, 16.0f, 0.001f);
}

TEST(ManualClock, DeltaSinceReturnsAdvanceAmount) {
    ManualClock clock;
    const auto t0 = clock.now();
    clock.advance_ms(33.0f);
    const auto dt = clock.delta_since(t0).count();
    EXPECT_NEAR(dt, 33.0f, 0.001f);
}

TEST(ManualClock, NegativeAdvanceIsClamped) {
    ManualClock clock;
    const auto t0 = clock.now();
    clock.advance(DurationMs(-100.0f));  // must be ignored
    const auto t1 = clock.now();
    EXPECT_EQ(t0, t1);  // No change — monotonic guarantee
}

TEST(ManualClock, CumulativeAdvance) {
    ManualClock clock;
    const auto t0 = clock.now();
    clock.advance_ms(10.0f);
    clock.advance_ms(10.0f);
    clock.advance_ms(10.0f);
    const auto dt = clock.delta_since(t0).count();
    EXPECT_NEAR(dt, 30.0f, 0.001f);
}

TEST(IClock, PolymorphicDispatch) {
    // Verify IClock& dispatches to the concrete clock. RealClock returns
    // a real time point; ManualClock returns the synthetic one.
    RealClock real;
    ManualClock manual;
    IClock& real_as_base = real;
    IClock& manual_as_base = manual;

    const auto t_real = real_as_base.now();
    const auto t_manual_0 = manual_as_base.now();
    manual.advance_ms(5.0f);
    const auto t_manual_1 = manual_as_base.now();

    EXPECT_GT(t_real, TimePoint{});  // Real clock is past its epoch.
    EXPECT_NE(t_manual_0, t_manual_1);  // Manual clock advanced.
}
