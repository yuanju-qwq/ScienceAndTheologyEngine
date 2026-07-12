// MachineTickSystem -- P7.2 deterministic machine processing boundary.
//
// This module owns no AngelScript VM state. It copies a recipe definition
// from RegistryHub when a machine starts work, then completes that immutable
// snapshot even if the script module is hot-reloaded in the meantime.
// Future UI, save, and replication modules subscribe through
// IMachineTickEventSink rather than reaching into machine state directly.
// MachineTickSystem captures RegistryHub and World data on the main thread,
// computes a self-contained patch on a worker, and publishes events only
// while its command is applied back on the main thread.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ecs/entity_guid.h"
#include "ecs/system.h"

namespace snt::ecs {
class World;
}

namespace snt::script {
class RegistryHub;
}

namespace snt::gameplay {

struct MachineItemStack {
    std::string item_id;
    int32_t count = 0;

    bool empty() const { return item_id.empty() || count <= 0; }
};

// Engine-owned recipe copy. It deliberately mirrors only the data needed by
// the tick loop and never contains RegistryHub or AngelScript references.
struct MachineRecipeSnapshot {
    std::string id;
    std::string input_item_id;
    std::vector<MachineItemStack> outputs;
    int32_t duration_ticks = 0;
    int32_t energy_per_tick = 0;
};

enum class MachineRunState : uint8_t {
    Idle,
    Running,
    NoMatchingRecipe,
    WaitingForEnergy,
    WaitingForOutput,
};

// ECS component for one machine controller. Input is reserved when a recipe
// starts, so a hot reload or an inventory mutation cannot change a job that
// is already in progress. P7.4 serializes this component as gameplay state.
struct MachineRuntimeComponent {
    std::string machine_id;
    MachineItemStack input;
    std::vector<MachineItemStack> output_slots;

    int32_t stored_energy = 0;
    int32_t energy_capacity = 0;
    int32_t max_output_slots = 4;
    int32_t max_stack_size = 64;

    int32_t progress_ticks = 0;
    std::optional<MachineRecipeSnapshot> active_recipe;
    MachineRunState state = MachineRunState::Idle;
};

enum class MachineTickEventKind : uint8_t {
    StateChanged,
    RecipeCompleted,
};

struct MachineTickEvent {
    MachineTickEventKind kind = MachineTickEventKind::StateChanged;
    snt::ecs::EntityGuid entity_guid;
    std::string machine_id;
    std::string recipe_id;
    MachineRunState previous_state = MachineRunState::Idle;
    MachineRunState state = MachineRunState::Idle;
};

// Reserved cross-module contract. P7.3 quest progression, P7.4 persistence
// dirtiness, and P7.5 replication can implement this without changing the
// deterministic machine loop.
class IMachineTickEventSink {
public:
    virtual ~IMachineTickEventSink() = default;
    virtual void on_machine_tick_event(const MachineTickEvent& event) = 0;
};

class MachineTickSystem final : public snt::ecs::IWorkerSystem {
public:
    explicit MachineTickSystem(snt::script::RegistryHub& registries,
                               IMachineTickEventSink* event_sink = nullptr);

    snt::ecs::SystemMetadata metadata() const override {
        return {
            "gameplay.machine_tick",
            snt::ecs::SystemThreadAffinity::Worker,
            {
                {"ecs.machine_runtime", snt::ecs::SystemResourceAccessMode::Write},
                {"script.registry", snt::ecs::SystemResourceAccessMode::Read},
                {"gameplay.machine_events", snt::ecs::SystemResourceAccessMode::Write},
            },
        };
    }

    std::unique_ptr<snt::ecs::IWorkerTask> capture(
        const snt::ecs::World& world, float dt) override;

private:
    snt::script::RegistryHub& registries_;
    IMachineTickEventSink* event_sink_ = nullptr;
};

}  // namespace snt::gameplay
