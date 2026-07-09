// Unit tests for ECS components, World, and SystemScheduler (P2 tasks 5-6).
//
// Validates:
//   - Standard gameplay components (Position/Velocity/Health/Inventory).
//   - World entity creation + component assignment + query.
//   - SystemScheduler registration and frequency-based dispatch.
//   - Tag components (PlayerMarker/CreatureMarker/StaticMarker).

#include "ecs/components.h"
#include "ecs/world.h"
#include "ecs/system_scheduler.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace snt::ecs;
using snt::core::JobSystem;
using snt::core::JobSystemP2;
using snt::core::default_job_system;

// ===========================================================================
// Standard gameplay components
// ===========================================================================

TEST(StandardComponentsTest, PositionDefault) {
    Position p;
    EXPECT_EQ(p.x, 0);
    EXPECT_EQ(p.y, 0);
    EXPECT_EQ(p.z, 0);
}

TEST(StandardComponentsTest, VelocityDefault) {
    Velocity v;
    EXPECT_FLOAT_EQ(v.vx, 0.0f);
    EXPECT_FLOAT_EQ(v.vy, 0.0f);
    EXPECT_FLOAT_EQ(v.vz, 0.0f);
}

TEST(StandardComponentsTest, HealthDefaultAndHelpers) {
    Health h;
    EXPECT_FLOAT_EQ(h.current, 1.0f);
    EXPECT_FLOAT_EQ(h.maximum, 1.0f);
    EXPECT_FALSE(h.is_dead());
    EXPECT_FLOAT_EQ(h.fraction(), 1.0f);

    h.current = 0.0f;
    EXPECT_TRUE(h.is_dead());

    h.current = 0.5f;
    h.maximum = 2.0f;
    EXPECT_FLOAT_EQ(h.fraction(), 0.25f);
}

TEST(StandardComponentsTest, InventoryDefault) {
    Inventory inv;
    EXPECT_TRUE(inv.slots.empty());
    EXPECT_EQ(inv.max_slots, 16);
}

// ===========================================================================
// World entity/component management
// ===========================================================================

TEST(WorldComponentTest, CreateEntityWithPosition) {
    World world;
    auto e = world.create_entity();
    world.add_component<Position>(e, Position{10, 20, 30});

    auto& pos = world.get_component<Position>(e);
    EXPECT_EQ(pos.x, 10);
    EXPECT_EQ(pos.y, 20);
    EXPECT_EQ(pos.z, 30);
}

TEST(WorldComponentTest, CreateEntityWithHealth) {
    World world;
    auto e = world.create_entity();
    world.add_component<Health>(e, Health{50.0f, 100.0f});

    auto& hp = world.get_component<Health>(e);
    EXPECT_FLOAT_EQ(hp.current, 50.0f);
    EXPECT_FLOAT_EQ(hp.maximum, 100.0f);
    EXPECT_FALSE(hp.is_dead());
}

TEST(WorldComponentTest, CreateEntityWithVelocityAndPosition) {
    World world;
    auto e = world.create_entity();
    world.add_component<Position>(e, Position{0, 0, 0});
    world.add_component<Velocity>(e, Velocity{1.0f, 0.0f, 0.0f});

    auto& pos = world.get_component<Position>(e);
    auto& vel = world.get_component<Velocity>(e);

    // Simulate one tick of movement integration.
    pos.x += static_cast<int32_t>(vel.vx);
    EXPECT_EQ(pos.x, 1);
}

TEST(WorldComponentTest, DestroyEntityRemovesComponents) {
    World world;
    auto e = world.create_entity();
    world.add_component<Position>(e, Position{5, 5, 5});

    auto& pos = world.get_component<Position>(e);
    EXPECT_EQ(pos.x, 5);

    world.destroy_entity(e);
    // Entity destroyed; querying via registry view would return no match.
    EXPECT_FALSE(world.registry().valid(e));
}

TEST(WorldComponentTest, TagComponents) {
    World world;
    auto player = world.create_entity();
    world.registry().emplace<PlayerMarker>(player);

    auto creature = world.create_entity();
    world.registry().emplace<CreatureMarker>(creature);

    auto machine = world.create_entity();
    world.registry().emplace<StaticMarker>(machine);

    // Tags are present on their owners and absent from others.
    EXPECT_TRUE(world.registry().all_of<PlayerMarker>(player));
    EXPECT_TRUE(world.registry().all_of<CreatureMarker>(creature));
    EXPECT_TRUE(world.registry().all_of<StaticMarker>(machine));
    EXPECT_FALSE(world.registry().all_of<CreatureMarker>(player));
}

// ===========================================================================
// SystemScheduler
// ===========================================================================

// A test system that increments a counter when updated.
class CountingSystem : public System {
public:
    void update(World& world, float dt) override {
        (void)world;
        (void)dt;
        ++count;
    }
    std::atomic<int> count{0};
};

TEST(SystemSchedulerTest, RegisterAndTickMainThreadSystem) {
    SystemScheduler sched(nullptr);
    auto sys = std::make_shared<CountingSystem>();
    sched.add_main_thread(sys, "counter", 60);

    World world;
    sched.update(world, 1.0f / 60.0f);  // one frame at 60Hz

    EXPECT_EQ(sys->count.load(), 1);
}

TEST(SystemSchedulerTest, FrequencyControlSkipsExcessFrames) {
    SystemScheduler sched(nullptr);
    auto sys = std::make_shared<CountingSystem>();
    // 10Hz system: should only tick once per 100ms.
    sched.add_main_thread(sys, "slow", 10);

    World world;
    // Simulate 5 frames at 60Hz (83ms total) — should NOT tick yet
    // because 83ms < 100ms.
    for (int i = 0; i < 5; ++i) {
        sched.update(world, 1.0f / 60.0f);
    }
    EXPECT_EQ(sys->count.load(), 0);

    // One more frame (~100ms total) — should tick once.
    sched.update(world, 1.0f / 60.0f);
    EXPECT_EQ(sys->count.load(), 1);
}

TEST(SystemSchedulerTest, DisabledSystemDoesNotTick) {
    SystemScheduler sched(nullptr);
    auto sys = std::make_shared<CountingSystem>();
    sched.add_main_thread(sys, "counter", 60);

    // Disable the system via the systems() vector.
    sched.systems()[0].enabled = false;

    World world;
    sched.update(world, 1.0f / 60.0f);
    EXPECT_EQ(sys->count.load(), 0);
}

TEST(SystemSchedulerTest, WorkerPoolDispatchesViaJobSystem) {
    // Use the real P2 Job System so the worker-pool path is exercised.
    JobSystemP2 js;
    js.init(2);

    SystemScheduler sched(&js);
    auto sys = std::make_shared<CountingSystem>();
    sched.add_worker(sys, "worker_counter", 60);

    World world;
    sched.update(world, 1.0f / 60.0f);

    // Wait for the worker to complete by draining the job system.
    // The scheduler is fire-and-forget, so we sleep briefly to let the
    // worker thread run. This is a best-effort synchronization for tests.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_GE(sys->count.load(), 1);

    js.shutdown();
}

TEST(SystemSchedulerTest, AsyncSystemDoesNotTickOnUpdate) {
    SystemScheduler sched(nullptr);
    auto sys = std::make_shared<CountingSystem>();
    sched.add_async(sys, "async_counter");

    World world;
    sched.update(world, 1.0f / 60.0f);
    EXPECT_EQ(sys->count.load(), 0);
}

TEST(SystemSchedulerTest, TriggerAsyncRunsSystem) {
    SystemScheduler sched(nullptr);
    auto sys = std::make_shared<CountingSystem>();
    sched.add_async(sys, "async_counter");

    World world;
    sched.trigger_async(world, "async_counter");
    EXPECT_EQ(sys->count.load(), 1);
}

TEST(SystemSchedulerTest, SystemCount) {
    SystemScheduler sched(nullptr);
    sched.add_main_thread(std::make_shared<CountingSystem>(), "a", 60);
    sched.add_worker(std::make_shared<CountingSystem>(), "b", 20);
    sched.add_async(std::make_shared<CountingSystem>(), "c");

    EXPECT_EQ(sched.system_count(), 3u);
}
