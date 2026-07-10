// MachineTickSystem -- P7.2 implementation.

#define SNT_LOG_CHANNEL "gameplay"
#include "gameplay/machine_tick_system.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/log.h"
#include "ecs/world.h"
#include "script/registry_hub.h"

namespace snt::gameplay {
namespace {

const char* state_name(MachineRunState state) {
    switch (state) {
        case MachineRunState::Idle: return "idle";
        case MachineRunState::Running: return "running";
        case MachineRunState::NoMatchingRecipe: return "no_matching_recipe";
        case MachineRunState::WaitingForEnergy: return "waiting_for_energy";
        case MachineRunState::WaitingForOutput: return "waiting_for_output";
    }
    return "unknown";
}

MachineRecipeSnapshot make_snapshot(const snt::script::RecipeDefinition& recipe) {
    MachineRecipeSnapshot snapshot;
    snapshot.id = recipe.id;
    snapshot.input_item_id = recipe.input_item_id;
    snapshot.duration_ticks = recipe.duration_ticks;
    snapshot.energy_per_tick = recipe.energy_per_tick;
    snapshot.outputs.reserve(recipe.outputs.size());
    for (const auto& output : recipe.outputs) {
        snapshot.outputs.push_back({output.item_id, output.count});
    }
    return snapshot;
}

void normalize_stack(MachineItemStack& stack) {
    if (stack.count <= 0 || stack.item_id.empty()) {
        stack.item_id.clear();
        stack.count = 0;
    }
}

void normalize_output_slots(MachineRuntimeComponent& machine) {
    for (auto& slot : machine.output_slots) normalize_stack(slot);
    std::erase_if(machine.output_slots, [](const MachineItemStack& slot) {
        return slot.empty();
    });
}

bool insert_outputs(std::vector<MachineItemStack>& slots,
                    int32_t max_slots,
                    int32_t max_stack_size,
                    const std::vector<MachineItemStack>& outputs) {
    if (max_slots <= 0 || max_stack_size <= 0) return false;

    for (const auto& output : outputs) {
        if (output.empty()) return false;
        auto existing = std::find_if(slots.begin(), slots.end(), [&output](const auto& slot) {
            return slot.item_id == output.item_id;
        });
        if (existing != slots.end()) {
            if (output.count > max_stack_size - existing->count) return false;
            existing->count += output.count;
            continue;
        }
        if (slots.size() >= static_cast<size_t>(max_slots) || output.count > max_stack_size) {
            return false;
        }
        slots.push_back(output);
    }
    return true;
}

bool can_accept_outputs(const MachineRuntimeComponent& machine,
                        const MachineRecipeSnapshot& recipe) {
    std::vector<MachineItemStack> candidate = machine.output_slots;
    return insert_outputs(candidate,
                          machine.max_output_slots,
                          machine.max_stack_size,
                          recipe.outputs);
}

bool commit_outputs(MachineRuntimeComponent& machine,
                    const MachineRecipeSnapshot& recipe) {
    std::vector<MachineItemStack> candidate = machine.output_slots;
    if (!insert_outputs(candidate,
                        machine.max_output_slots,
                        machine.max_stack_size,
                        recipe.outputs)) {
        return false;
    }
    machine.output_slots = std::move(candidate);
    return true;
}

bool is_diagnostic_state(MachineRunState state) {
    return state == MachineRunState::NoMatchingRecipe ||
           state == MachineRunState::WaitingForEnergy ||
           state == MachineRunState::WaitingForOutput;
}

}  // namespace

MachineTickSystem::MachineTickSystem(snt::script::RegistryHub& registries,
                                     IMachineTickEventSink* event_sink)
    : registries_(registries)
    , event_sink_(event_sink) {}

void MachineTickSystem::update(snt::ecs::World& world, float /*dt*/) {
    std::vector<entt::entity> entities;
    const auto view = world.registry().view<MachineRuntimeComponent>();
    for (const entt::entity entity : view) entities.push_back(entity);

    // Registry storage order is not a cross-run simulation contract. Stable
    // EntityGuid order makes recipe selection and event ordering deterministic.
    std::sort(entities.begin(), entities.end(), [&world](entt::entity lhs, entt::entity rhs) {
        return world.guid_of(lhs).value < world.guid_of(rhs).value;
    });

    for (const entt::entity entity : entities) {
        MachineRuntimeComponent& machine = world.registry().get<MachineRuntimeComponent>(entity);
        const snt::ecs::EntityGuid entity_guid = world.guid_of(entity);
        normalize_stack(machine.input);
        normalize_output_slots(machine);
        machine.stored_energy = std::max(machine.stored_energy, 0);

        if (!machine.active_recipe) {
            if (machine.input.empty()) {
                transition(entity_guid, machine, MachineRunState::Idle, "");
                continue;
            }

            const std::vector<snt::script::RecipeDefinition> recipes =
                registries_.recipes_for_machine(machine.machine_id);
            const auto recipe = std::find_if(recipes.begin(), recipes.end(), [&machine](const auto& value) {
                return value.input_item_id == machine.input.item_id;
            });
            if (recipe == recipes.end()) {
                transition(entity_guid, machine, MachineRunState::NoMatchingRecipe, "");
                continue;
            }

            MachineRecipeSnapshot snapshot = make_snapshot(*recipe);
            if (!can_accept_outputs(machine, snapshot)) {
                transition(entity_guid, machine, MachineRunState::WaitingForOutput, snapshot.id);
                continue;
            }

            // Reserve exactly one input at start. This is the transaction
            // point that makes an active snapshot immune to later inventory
            // changes and script reloads.
            --machine.input.count;
            normalize_stack(machine.input);
            machine.progress_ticks = 0;
            machine.active_recipe = std::move(snapshot);
        }

        MachineRecipeSnapshot& active = *machine.active_recipe;
        if (machine.progress_ticks >= active.duration_ticks) {
            if (!commit_outputs(machine, active)) {
                transition(entity_guid, machine, MachineRunState::WaitingForOutput, active.id);
                continue;
            }
            const std::string recipe_id = active.id;
            machine.active_recipe.reset();
            machine.progress_ticks = 0;
            publish_completion(entity_guid, machine, recipe_id);
            transition(entity_guid, machine, MachineRunState::Idle, recipe_id);
            continue;
        }

        if (active.energy_per_tick > machine.stored_energy) {
            transition(entity_guid, machine, MachineRunState::WaitingForEnergy, active.id);
            continue;
        }

        machine.stored_energy -= active.energy_per_tick;
        ++machine.progress_ticks;
        transition(entity_guid, machine, MachineRunState::Running, active.id);

        if (machine.progress_ticks < active.duration_ticks) continue;

        if (!commit_outputs(machine, active)) {
            transition(entity_guid, machine, MachineRunState::WaitingForOutput, active.id);
            continue;
        }
        const std::string recipe_id = active.id;
        machine.active_recipe.reset();
        machine.progress_ticks = 0;
        publish_completion(entity_guid, machine, recipe_id);
        transition(entity_guid, machine, MachineRunState::Idle, recipe_id);
    }
}

void MachineTickSystem::transition(snt::ecs::EntityGuid entity_guid,
                                   MachineRuntimeComponent& machine,
                                   MachineRunState state,
                                   const std::string& recipe_id) {
    if (machine.state == state) return;

    const MachineRunState previous_state = machine.state;
    machine.state = state;

    // Log only actionable diagnostics and recovery. Running/idle transitions
    // happen for every crafted item and must never become a tick-rate log.
    if (is_diagnostic_state(previous_state) || is_diagnostic_state(state)) {
        SNT_LOG_INFO("P7 machine %llu (%s) state %s -> %s%s%s",
                     static_cast<unsigned long long>(entity_guid.value),
                     machine.machine_id.c_str(),
                     state_name(previous_state),
                     state_name(state),
                     recipe_id.empty() ? "" : " recipe=",
                     recipe_id.empty() ? "" : recipe_id.c_str());
    }

    if (event_sink_) {
        event_sink_->on_machine_tick_event({
            .kind = MachineTickEventKind::StateChanged,
            .entity_guid = entity_guid,
            .machine_id = machine.machine_id,
            .recipe_id = recipe_id,
            .previous_state = previous_state,
            .state = state,
        });
    }
}

void MachineTickSystem::publish_completion(snt::ecs::EntityGuid entity_guid,
                                           const MachineRuntimeComponent& machine,
                                           const std::string& recipe_id) {
    if (!event_sink_) return;
    event_sink_->on_machine_tick_event({
        .kind = MachineTickEventKind::RecipeCompleted,
        .entity_guid = entity_guid,
        .machine_id = machine.machine_id,
        .recipe_id = recipe_id,
        .previous_state = machine.state,
        .state = machine.state,
    });
}

}  // namespace snt::gameplay
