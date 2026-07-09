// World ECS components — data extracted from the legacy WorldData god-object.
//
// P2 task 4: radical refactor. WorldData was a god-object holding chunks,
// gameplay config, tick state, worldgen config, physics events, block
// entities, machine collision, and mobile structures. The refactor splits
// these into:
//   - ChunkRegistry (chunk storage)              -> data/world/chunk_registry.h
//   - BlockEntityRegistry (block entities)       -> data/defs/block_entity_registry.h
//   - MachineCollisionOverlay (machine collision)-> data/defs/machine_collision_overlay.h
//   - DynamicStructureRegistry (mobile structs)  -> data/mobile_structure/dynamic_structure.h
//   - ECS components below (config/tick/events)  -> this file
//
// These components are attached to a singleton "world context" entity in the
// ECS World. Systems query them via world.ctx<>() (EnTT singleton component).
//
// Thread safety: single-threaded (main thread). Systems that run on the
// worker pool must snapshot these values before dispatch.

#pragma once

#include <cstdint>
#include <memory>
#include <queue>
#include <string>

#include "data/defs/gameplay_config.h"
#include "data/world_gen/world_gen_config.h"

namespace snt::data {

// A block physics event enqueued after a block is mined.
// BlockPhysicsSystem consumes these each tick.
struct BlockPhysicsEvent {
    std::string dimension_id;
    int block_x = 0;
    int block_y = 0;
    int block_z = 0;
};

// ECS component: runtime gameplay configuration (collapse, gravity fall, etc.).
// Mutable at runtime, separate from frozen WorldGenConfigSnapshot.
// Attached as a singleton to the world context entity.
struct GameplayConfigComponent {
    GameplayConfig config;
};

// ECS component: current simulation tick counter.
// Set by TickSystem each frame. Subsystems read this instead of maintaining
// their own counters.
// Attached as a singleton to the world context entity.
struct TickComponent {
    int64_t current_tick = 0;
};

// ECS component: frozen world generation config snapshot reference.
// Provides access to PlanetConfig, material definitions, etc. for physics
// and generation systems. The snapshot is immutable once loaded.
// Attached as a singleton to the world context entity.
struct WorldGenConfigComponent {
    std::shared_ptr<const WorldGenConfigSnapshot> config;
};

// ECS component: block physics event queue.
// Producer: mining/placement systems push events here.
// Consumer: BlockPhysicsSystem drains the queue each tick.
// Attached as a singleton to the world context entity.
struct BlockPhysicsEventQueue {
    std::queue<BlockPhysicsEvent> events;

    void push(const BlockPhysicsEvent& event) {
        events.push(event);
    }

    bool pop(BlockPhysicsEvent& out) {
        if (events.empty()) return false;
        out = events.front();
        events.pop();
        return true;
    }

    size_t count() const { return events.size(); }
};

} // namespace snt::data
