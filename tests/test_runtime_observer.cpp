#include "engine/runtime_observer.h"

#include <gtest/gtest.h>

namespace {

class RecordingObserver final : public snt::engine::IRuntimeObserver {
public:
    void on_lifecycle_changed(
        const snt::engine::RuntimeObserverSnapshot& snapshot) override {
        lifecycle_snapshot = snapshot;
        lifecycle_calls++;
    }

    void on_statistics_updated(
        const snt::engine::RuntimeObserverSnapshot& snapshot) override {
        statistics_snapshot = snapshot;
        statistics_calls++;
    }

    snt::engine::RuntimeObserverSnapshot lifecycle_snapshot{};
    snt::engine::RuntimeObserverSnapshot statistics_snapshot{};
    int lifecycle_calls = 0;
    int statistics_calls = 0;
};

}  // namespace

TEST(RuntimeObserverContract, ReceivesCopyableValueSnapshots) {
    RecordingObserver observer;
    snt::engine::RuntimeObserverSnapshot snapshot;
    snapshot.phase = snt::engine::RuntimeLifecyclePhase::Running;
    snapshot.fixed_tick_index = 42;
    snapshot.stats.fps = 120.0f;
    snapshot.stats.tps = 20.0f;

    observer.on_lifecycle_changed(snapshot);
    observer.on_statistics_updated(snapshot);

    EXPECT_EQ(observer.lifecycle_calls, 1);
    EXPECT_EQ(observer.statistics_calls, 1);
    EXPECT_EQ(observer.lifecycle_snapshot.phase,
              snt::engine::RuntimeLifecyclePhase::Running);
    EXPECT_EQ(observer.statistics_snapshot.fixed_tick_index, 42u);
    EXPECT_FLOAT_EQ(observer.statistics_snapshot.stats.fps, 120.0f);
    EXPECT_FLOAT_EQ(observer.statistics_snapshot.stats.tps, 20.0f);
}
