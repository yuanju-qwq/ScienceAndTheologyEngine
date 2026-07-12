// P7.2 tests -- deterministic machine runtime and reload-safe snapshots.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/job_system.h"
#include "ecs/system_scheduler.h"
#include "ecs/world.h"
#include "gameplay/machine_tick_system.h"
#include "script/registry_hub.h"

namespace {

using snt::gameplay::MachineItemStack;
using snt::gameplay::MachineRunState;
using snt::gameplay::MachineRuntimeComponent;
using snt::gameplay::MachineTickEvent;
using snt::gameplay::MachineTickEventKind;
using snt::gameplay::MachineTickSystem;
using snt::gameplay::IMachineTickEventSink;
using snt::script::RecipeDefinition;
using snt::script::RecipeOutputDefinition;
using snt::script::RegistryHub;

RecipeDefinition make_recipe(std::string id,
                             std::string input,
                             std::string output,
                             int duration_ticks,
                             int energy_per_tick = 0) {
    RecipeDefinition recipe;
    recipe.id = std::move(id);
    recipe.machine_id = "furnace";
    recipe.input_item_id = std::move(input);
    recipe.outputs = {RecipeOutputDefinition{std::move(output), 1}};
    recipe.duration_ticks = duration_ticks;
    recipe.energy_per_tick = energy_per_tick;
    return recipe;
}

class CapturingMachineEvents final : public IMachineTickEventSink {
public:
    void on_machine_tick_event(const MachineTickEvent& event) override {
        events.push_back(event);
    }

    std::vector<MachineTickEvent> events;
};

bool tick_machine(snt::ecs::World& world,
                  const std::shared_ptr<MachineTickSystem>& system) {
    snt::core::JobSystem jobs;
    snt::ecs::SystemScheduler scheduler(jobs);
    if (!scheduler.register_worker(system)) return false;
    return static_cast<bool>(scheduler.fixed_tick(world, 0.05f));
}

}  // namespace

TEST(MachineTickSystemTest, ProcessesFurnaceRecipeAndPublishesCompletion) {
    RegistryHub registries;
    ASSERT_TRUE(registries.register_builtin_recipe(
        make_recipe("snt.furnace.iron", "iron_ore", "iron_ingot", 3)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input = {"iron_ore", 2};

    CapturingMachineEvents events;
    auto system = std::make_shared<MachineTickSystem>(registries, &events);
    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_TRUE(tick_machine(world, system));

    EXPECT_EQ(machine.state, MachineRunState::Idle);
    EXPECT_FALSE(machine.active_recipe.has_value());
    EXPECT_EQ(machine.input.item_id, "iron_ore");
    EXPECT_EQ(machine.input.count, 1);
    ASSERT_EQ(machine.output_slots.size(), 1U);
    EXPECT_EQ(machine.output_slots[0].item_id, "iron_ingot");
    EXPECT_EQ(machine.output_slots[0].count, 1);

    ASSERT_GE(events.events.size(), 2U);
    EXPECT_EQ(events.events[events.events.size() - 2].kind,
              MachineTickEventKind::RecipeCompleted);
    EXPECT_EQ(events.events.back().kind, MachineTickEventKind::StateChanged);
}

TEST(MachineTickSystemTest, WorkerPoolAppliesMachineCommandAtBarrier) {
    RegistryHub registries;
    ASSERT_TRUE(registries.register_builtin_recipe(
        make_recipe("snt.furnace.worker", "tin_ore", "tin_ingot", 2)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input = {"tin_ore", 1};

    snt::core::JobSystemP2 jobs;
    jobs.init(2);
    {
        auto system = std::make_shared<MachineTickSystem>(registries);
        snt::ecs::SystemScheduler scheduler(jobs);
        ASSERT_TRUE(scheduler.register_worker(system));
        ASSERT_TRUE(scheduler.fixed_tick(world, 0.05f));

        EXPECT_EQ(scheduler.diagnostics().worker_tasks_submitted, 1u);
        EXPECT_EQ(scheduler.diagnostics().commands_applied, 1u);
        EXPECT_EQ(machine.state, MachineRunState::Running);
        EXPECT_EQ(machine.progress_ticks, 1);
        EXPECT_EQ(machine.input.count, 0);
    }
    jobs.shutdown();
}

TEST(MachineTickSystemTest, WorkerPoolShardsMachinesAndPublishesGuidOrder) {
    RegistryHub registries;
    ASSERT_TRUE(registries.register_builtin_recipe(
        make_recipe("snt.furnace.sharded", "lead_ore", "lead_ingot", 1)));

    constexpr int kMachineCount = 64;
    snt::ecs::World world;
    std::vector<entt::entity> entities;
    std::vector<snt::ecs::EntityGuid> expected_guids;
    entities.reserve(kMachineCount);
    expected_guids.reserve(kMachineCount);
    for (int index = 0; index < kMachineCount; ++index) {
        const entt::entity entity = world.create_entity();
        auto& machine = world.add_component<MachineRuntimeComponent>(entity);
        machine.machine_id = "furnace";
        machine.input = {"lead_ore", 1};
        entities.push_back(entity);
        expected_guids.push_back(world.guid_of(entity));
    }
    std::sort(expected_guids.begin(), expected_guids.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.value < rhs.value;
              });

    CapturingMachineEvents events;
    snt::core::JobSystemP2 jobs;
    jobs.init(2);
    {
        auto system = std::make_shared<MachineTickSystem>(registries, &events);
        snt::ecs::SystemScheduler scheduler(jobs);
        ASSERT_TRUE(scheduler.register_worker(system));
        ASSERT_TRUE(scheduler.fixed_tick(world, 0.05f));

        EXPECT_EQ(scheduler.diagnostics().worker_tasks_submitted, 1u);
        EXPECT_EQ(scheduler.diagnostics().worker_parallel_for_calls, 1u);
        EXPECT_EQ(scheduler.diagnostics().worker_parallel_for_items,
                  static_cast<uint64_t>(kMachineCount));
        EXPECT_EQ(scheduler.diagnostics().commands_applied,
                  static_cast<uint64_t>(kMachineCount));
    }
    jobs.shutdown();

    ASSERT_EQ(events.events.size(), static_cast<size_t>(kMachineCount * 3));
    for (int index = 0; index < kMachineCount; ++index) {
        const size_t event_offset = static_cast<size_t>(index * 3);
        const MachineTickEvent& started = events.events[event_offset];
        const MachineTickEvent& completed = events.events[event_offset + 1];
        const MachineTickEvent& idled = events.events[event_offset + 2];
        EXPECT_EQ(started.kind, MachineTickEventKind::StateChanged);
        EXPECT_EQ(completed.kind, MachineTickEventKind::RecipeCompleted);
        EXPECT_EQ(idled.kind, MachineTickEventKind::StateChanged);
        EXPECT_EQ(started.entity_guid, expected_guids[static_cast<size_t>(index)]);
        EXPECT_EQ(completed.entity_guid, expected_guids[static_cast<size_t>(index)]);
        EXPECT_EQ(idled.entity_guid, expected_guids[static_cast<size_t>(index)]);
        EXPECT_EQ(started.state, MachineRunState::Running);
        EXPECT_EQ(idled.state, MachineRunState::Idle);

        const auto& machine = world.get_component<MachineRuntimeComponent>(
            entities[static_cast<size_t>(index)]);
        EXPECT_EQ(machine.state, MachineRunState::Idle);
        EXPECT_FALSE(machine.active_recipe.has_value());
        EXPECT_TRUE(machine.input.empty());
        ASSERT_EQ(machine.output_slots.size(), 1u);
        EXPECT_EQ(machine.output_slots[0].item_id, "lead_ingot");
        EXPECT_EQ(machine.output_slots[0].count, 1);
    }
}

TEST(MachineTickSystemTest, ActiveRecipeSnapshotSurvivesScriptReload) {
    RegistryHub registries;
    ASSERT_TRUE(registries.register_script_recipe(
        7, make_recipe("snt.furnace.snapshot", "copper_ore", "copper_ingot", 2)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input = {"copper_ore", 2};

    auto system = std::make_shared<MachineTickSystem>(registries);
    ASSERT_TRUE(tick_machine(world, system));  // Starts the copied copper_ingot snapshot.
    ASSERT_TRUE(machine.active_recipe.has_value());
    EXPECT_EQ(machine.active_recipe->outputs[0].item_id, "copper_ingot");

    ASSERT_TRUE(registries.begin_reload(7));
    ASSERT_TRUE(registries.register_script_recipe(
        7, make_recipe("snt.furnace.snapshot", "copper_ore", "bronze_ingot", 2)));
    ASSERT_TRUE(registries.commit_reload(7));

    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_EQ(machine.output_slots.size(), 1U);
    EXPECT_EQ(machine.output_slots[0].item_id, "copper_ingot");

    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_EQ(machine.output_slots.size(), 2U);
    EXPECT_EQ(machine.output_slots[1].item_id, "bronze_ingot");
}

TEST(MachineTickSystemTest, ReservesInputAndWaitsForEnergyWithoutProgressing) {
    RegistryHub registries;
    ASSERT_TRUE(registries.register_builtin_recipe(
        make_recipe("snt.furnace.powered", "gold_ore", "gold_ingot", 2, 5)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input = {"gold_ore", 1};
    machine.stored_energy = 5;

    auto system = std::make_shared<MachineTickSystem>(registries);
    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.progress_ticks, 1);
    EXPECT_EQ(machine.input.count, 0);
    EXPECT_EQ(machine.stored_energy, 0);

    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.state, MachineRunState::WaitingForEnergy);
    EXPECT_EQ(machine.progress_ticks, 1);
    EXPECT_TRUE(machine.output_slots.empty());

    machine.stored_energy = 5;
    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.state, MachineRunState::Idle);
    ASSERT_EQ(machine.output_slots.size(), 1U);
    EXPECT_EQ(machine.output_slots[0].item_id, "gold_ingot");
}
