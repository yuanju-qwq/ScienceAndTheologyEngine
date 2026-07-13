// Unit tests for ECS components, World, and SystemScheduler.
//
// Validates:
//   - Neutral simulation components (Position/Velocity).
//   - World entity creation + component assignment + query.
//   - SystemScheduler metadata, fixed-tick barrier, and command ordering.

#include "ecs/core_components.h"
#include "ecs/world.h"
#include "ecs/system_scheduler.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace snt::ecs;
using snt::core::JobSystem;
using snt::core::JobSystemP2;

// ===========================================================================
// Core simulation components
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

// ===========================================================================
// SystemScheduler
// ===========================================================================

namespace {

SystemMetadata main_metadata(
    std::string name,
    std::vector<SystemResourceAccess> resources = {}) {
    return SystemMetadata{
        std::move(name),
        SystemThreadAffinity::MainThread,
        std::move(resources),
    };
}

SystemMetadata worker_metadata(
    std::string name,
    std::vector<SystemResourceAccess> resources = {}) {
    return SystemMetadata{
        std::move(name),
        SystemThreadAffinity::Worker,
        std::move(resources),
    };
}

// Test-only main-thread system with explicit scheduler metadata.
class CountingSystem final : public System {
public:
    explicit CountingSystem(SystemMetadata metadata)
        : metadata_(std::move(metadata)) {}

    SystemMetadata metadata() const override { return metadata_; }

    void update(World& world, float dt) override {
        (void)world;
        (void)dt;
        ++count;
    }

    std::atomic<int> count{0};

private:
    SystemMetadata metadata_;
};

// Adapts concise lambdas to the production worker contracts. The task itself
// receives only WorkerCommandContext, which prevents it from retaining World.
class FunctionWorkerTask final : public IWorkerTask {
public:
    using ExecuteFunction = std::function<void(WorkerCommandContext&)>;

    explicit FunctionWorkerTask(ExecuteFunction execute)
        : execute_(std::move(execute)) {}

    void execute(WorkerCommandContext& commands) override {
        execute_(commands);
    }

private:
    ExecuteFunction execute_;
};

class FunctionWorkerSystem final : public IWorkerSystem {
public:
    using CaptureFunction = std::function<std::unique_ptr<IWorkerTask>(
        const World&, float)>;

    FunctionWorkerSystem(SystemMetadata metadata, CaptureFunction capture)
        : metadata_(std::move(metadata)), capture_(std::move(capture)) {}

    SystemMetadata metadata() const override { return metadata_; }

    std::unique_ptr<IWorkerTask> capture(const World& world, float dt) override {
        return capture_(world, dt);
    }

private:
    SystemMetadata metadata_;
    CaptureFunction capture_;
};

}  // namespace

TEST(SystemSchedulerTest, RejectsInvalidMetadata) {
    JobSystem jobs;
    SystemScheduler scheduler(jobs);

    EXPECT_FALSE(scheduler.register_main(
        std::make_shared<CountingSystem>(main_metadata(""))));
    EXPECT_FALSE(scheduler.register_main(std::make_shared<CountingSystem>(
        main_metadata("blank-resource", {
            SystemResourceAccess{"", SystemResourceAccessMode::Read},
        }))));
    EXPECT_FALSE(scheduler.register_main(std::make_shared<CountingSystem>(
        worker_metadata("worker-on-main"))));

    auto valid_main = scheduler.register_main(
        std::make_shared<CountingSystem>(main_metadata("main")));
    ASSERT_TRUE(valid_main);

    EXPECT_FALSE(scheduler.register_main(
        std::make_shared<CountingSystem>(main_metadata("main"))));
    EXPECT_FALSE(scheduler.register_worker(std::make_shared<FunctionWorkerSystem>(
        main_metadata("main-affinity-worker"),
        [](const World&, float) -> std::unique_ptr<IWorkerTask> {
            return nullptr;
        })));
}

TEST(SystemSchedulerTest, RegisteredMainSystemTicksInRegistrationOrder) {
    JobSystem jobs;
    SystemScheduler scheduler(jobs);
    auto system = std::make_shared<CountingSystem>(main_metadata("counter"));
    auto registered = scheduler.register_main(system);
    ASSERT_TRUE(registered);
    EXPECT_EQ(*registered, 0u);

    World world;
    ASSERT_TRUE(scheduler.fixed_tick(world, 1.0f / 20.0f));

    EXPECT_EQ(system->count.load(), 1);
    EXPECT_EQ(scheduler.diagnostics().fixed_ticks, 1u);
    EXPECT_EQ(scheduler.diagnostics().main_system_updates, 1u);

    const auto systems = scheduler.systems();
    ASSERT_EQ(systems.size(), 1u);
    EXPECT_EQ(systems.front().metadata.name, "counter");
    EXPECT_FALSE(systems.front().worker);
}

TEST(SystemSchedulerTest, DisabledSystemDoesNotTick) {
    JobSystem jobs;
    SystemScheduler scheduler(jobs);
    auto system = std::make_shared<CountingSystem>(main_metadata("counter"));
    auto registered = scheduler.register_main(system);
    ASSERT_TRUE(registered);
    ASSERT_TRUE(scheduler.set_enabled(*registered, false));

    World world;
    ASSERT_TRUE(scheduler.fixed_tick(world, 1.0f / 20.0f));
    EXPECT_EQ(system->count.load(), 0);
}

TEST(SystemSchedulerTest, WorkerUsesCapturedSnapshotAndAppliesCommandAtBarrier) {
    JobSystemP2 jobs;
    jobs.init(2);
    {
        SystemScheduler scheduler(jobs);
        World world;
        const auto entity = world.create_entity();
        world.add_component<Position>(entity, Position{4, 0, 0});

        std::atomic<int> captures{0};
        std::atomic<int> executions{0};
        auto worker = std::make_shared<FunctionWorkerSystem>(
            worker_metadata("snapshot", {
                SystemResourceAccess{"position", SystemResourceAccessMode::Write},
            }),
            [entity, &captures, &executions](const World& captured_world,
                                             float) -> std::unique_ptr<IWorkerTask> {
                const int32_t snapshot =
                    captured_world.get_component<Position>(entity).x;
                ++captures;
                return std::make_unique<FunctionWorkerTask>(
                    [entity, snapshot, &executions](WorkerCommandContext& commands) {
                        ++executions;
                        commands.enqueue([entity, snapshot](World& command_world) {
                            command_world.get_component<Position>(entity).x = snapshot + 1;
                        });
                    });
            });
        ASSERT_TRUE(scheduler.register_worker(worker));

        ASSERT_TRUE(scheduler.fixed_tick(world, 1.0f / 20.0f));
        EXPECT_EQ(captures.load(), 1);
        EXPECT_EQ(executions.load(), 1);
        EXPECT_EQ(world.get_component<Position>(entity).x, 5);
        EXPECT_EQ(scheduler.diagnostics().worker_tasks_submitted, 1u);
        EXPECT_EQ(scheduler.diagnostics().commands_applied, 1u);
    }
    jobs.shutdown();
}

TEST(SystemSchedulerTest, ConflictingWorkerResourcesCreateDependency) {
    JobSystemP2 jobs;
    jobs.init(2);
    {
        SystemScheduler scheduler(jobs);
        std::atomic<bool> first_completed{false};
        std::atomic<bool> second_observed_completion{false};

        auto first = std::make_shared<FunctionWorkerSystem>(
            worker_metadata("first", {
                SystemResourceAccess{"simulation.position", SystemResourceAccessMode::Write},
            }),
            [&first_completed](const World&, float) -> std::unique_ptr<IWorkerTask> {
                return std::make_unique<FunctionWorkerTask>(
                    [&first_completed](WorkerCommandContext&) {
                        first_completed.store(true);
                    });
            });
        auto second = std::make_shared<FunctionWorkerSystem>(
            worker_metadata("second", {
                SystemResourceAccess{"simulation.position", SystemResourceAccessMode::Read},
            }),
            [&first_completed, &second_observed_completion](const World&,
                                                              float) -> std::unique_ptr<IWorkerTask> {
                return std::make_unique<FunctionWorkerTask>(
                    [&first_completed, &second_observed_completion](WorkerCommandContext&) {
                        second_observed_completion.store(first_completed.load());
                    });
            });
        ASSERT_TRUE(scheduler.register_worker(first));
        ASSERT_TRUE(scheduler.register_worker(second));

        World world;
        ASSERT_TRUE(scheduler.fixed_tick(world, 1.0f / 20.0f));
        EXPECT_TRUE(first_completed.load());
        EXPECT_TRUE(second_observed_completion.load());
        EXPECT_EQ(scheduler.diagnostics().conflict_edges, 1u);
    }
    jobs.shutdown();
}

TEST(SystemSchedulerTest, WorkerCommandsApplyInRegistrationOrder) {
    JobSystemP2 jobs;
    jobs.init(2);
    {
        SystemScheduler scheduler(jobs);
        std::vector<int> application_order;

        auto first = std::make_shared<FunctionWorkerSystem>(
            worker_metadata("first", {
                SystemResourceAccess{"resource.first", SystemResourceAccessMode::Write},
            }),
            [&application_order](const World&, float) -> std::unique_ptr<IWorkerTask> {
                return std::make_unique<FunctionWorkerTask>(
                    [&application_order](WorkerCommandContext& commands) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        commands.enqueue([&application_order](World&) {
                            application_order.push_back(1);
                        });
                    });
            });
        auto second = std::make_shared<FunctionWorkerSystem>(
            worker_metadata("second", {
                SystemResourceAccess{"resource.second", SystemResourceAccessMode::Write},
            }),
            [&application_order](const World&, float) -> std::unique_ptr<IWorkerTask> {
                return std::make_unique<FunctionWorkerTask>(
                    [&application_order](WorkerCommandContext& commands) {
                        commands.enqueue([&application_order](World&) {
                            application_order.push_back(2);
                        });
                    });
            });
        ASSERT_TRUE(scheduler.register_worker(first));
        ASSERT_TRUE(scheduler.register_worker(second));

        World world;
        ASSERT_TRUE(scheduler.fixed_tick(world, 1.0f / 20.0f));
        ASSERT_EQ(application_order.size(), 2u);
        EXPECT_EQ(application_order[0], 1);
        EXPECT_EQ(application_order[1], 2);
        EXPECT_EQ(scheduler.diagnostics().conflict_edges, 0u);
    }
    jobs.shutdown();
}

TEST(SystemSchedulerTest, ShutdownRejectsFurtherScheduling) {
    JobSystem jobs;
    SystemScheduler scheduler(jobs);
    auto system = std::make_shared<CountingSystem>(main_metadata("counter"));
    auto registered = scheduler.register_main(system);
    ASSERT_TRUE(registered);

    scheduler.shutdown();
    EXPECT_TRUE(scheduler.is_shutdown());
    EXPECT_FALSE(scheduler.set_enabled(*registered, false));

    World world;
    EXPECT_FALSE(scheduler.fixed_tick(world, 1.0f / 20.0f));
    EXPECT_FALSE(scheduler.register_main(
        std::make_shared<CountingSystem>(main_metadata("after-shutdown"))));
}
