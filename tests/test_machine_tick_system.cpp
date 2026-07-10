// P7.2 tests -- deterministic machine runtime and reload-safe snapshots.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

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
    MachineTickSystem system(registries, &events);
    system.update(world, 0.05f);
    system.update(world, 0.05f);
    system.update(world, 0.05f);

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

TEST(MachineTickSystemTest, ActiveRecipeSnapshotSurvivesScriptReload) {
    RegistryHub registries;
    ASSERT_TRUE(registries.register_script_recipe(
        7, make_recipe("snt.furnace.snapshot", "copper_ore", "copper_ingot", 2)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input = {"copper_ore", 2};

    MachineTickSystem system(registries);
    system.update(world, 0.05f);  // Starts the copied copper_ingot snapshot.
    ASSERT_TRUE(machine.active_recipe.has_value());
    EXPECT_EQ(machine.active_recipe->outputs[0].item_id, "copper_ingot");

    ASSERT_TRUE(registries.begin_reload(7));
    ASSERT_TRUE(registries.register_script_recipe(
        7, make_recipe("snt.furnace.snapshot", "copper_ore", "bronze_ingot", 2)));
    ASSERT_TRUE(registries.commit_reload(7));

    system.update(world, 0.05f);
    ASSERT_EQ(machine.output_slots.size(), 1U);
    EXPECT_EQ(machine.output_slots[0].item_id, "copper_ingot");

    system.update(world, 0.05f);
    system.update(world, 0.05f);
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

    MachineTickSystem system(registries);
    system.update(world, 0.05f);
    EXPECT_EQ(machine.progress_ticks, 1);
    EXPECT_EQ(machine.input.count, 0);
    EXPECT_EQ(machine.stored_energy, 0);

    system.update(world, 0.05f);
    EXPECT_EQ(machine.state, MachineRunState::WaitingForEnergy);
    EXPECT_EQ(machine.progress_ticks, 1);
    EXPECT_TRUE(machine.output_slots.empty());

    machine.stored_energy = 5;
    system.update(world, 0.05f);
    EXPECT_EQ(machine.state, MachineRunState::Idle);
    ASSERT_EQ(machine.output_slots.size(), 1U);
    EXPECT_EQ(machine.output_slots[0].item_id, "gold_ingot");
}
