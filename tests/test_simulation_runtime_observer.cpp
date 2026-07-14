#include "engine/simulation_runtime_observer.h"

#include <gtest/gtest.h>

namespace {

class RecordingObserver final : public snt::engine::ISimulationRuntimeObserver {
public:
    void on_lifecycle_changed(
        const snt::engine::SimulationRuntimeObserverSnapshot& snapshot) override {
        lifecycle_snapshot = snapshot;
        lifecycle_calls++;
    }

    void on_statistics_updated(
        const snt::engine::SimulationRuntimeObserverSnapshot& snapshot) override {
        statistics_snapshot = snapshot;
        statistics_calls++;
    }

    snt::engine::SimulationRuntimeObserverSnapshot lifecycle_snapshot{};
    snt::engine::SimulationRuntimeObserverSnapshot statistics_snapshot{};
    int lifecycle_calls = 0;
    int statistics_calls = 0;
};

}  // namespace

TEST(SimulationRuntimeObserverContract, ReceivesCopyableValueSnapshots) {
    RecordingObserver observer;
    snt::engine::SimulationRuntimeObserverSnapshot snapshot;
    snapshot.phase = snt::engine::SimulationRuntimeLifecyclePhase::Running;
    snapshot.fixed_tick_index = 42;
    snapshot.stats.tps = 20.0f;
    snapshot.stats.mspt = 3.5f;

    observer.on_lifecycle_changed(snapshot);
    observer.on_statistics_updated(snapshot);

    EXPECT_EQ(observer.lifecycle_calls, 1);
    EXPECT_EQ(observer.statistics_calls, 1);
    EXPECT_EQ(observer.lifecycle_snapshot.phase,
              snt::engine::SimulationRuntimeLifecyclePhase::Running);
    EXPECT_EQ(observer.statistics_snapshot.fixed_tick_index, 42u);
    EXPECT_FLOAT_EQ(observer.statistics_snapshot.stats.tps, 20.0f);
    EXPECT_FLOAT_EQ(observer.statistics_snapshot.stats.mspt, 3.5f);
}
