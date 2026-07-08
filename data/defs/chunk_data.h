// ChunkData — world data shard for a fixed-size region of the voxel world.
//
// Ported from src/core/world/chunk_data.hpp.
// Namespace: science_and_theology -> snt::data.
//
// This is a pure data container, not a node. One chunk represents a
// fixed-size region (kChunkSize^3 cells) of the world.

#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <utility>
#include <vector>

#include "defs/terrain_data.h"
#include "defs/entity_data.h"
#include "defs/block_entity.h"
#include "defs/population_cell.h"
#include "defs/captive_creature.h"

namespace snt::data {

// Lifecycle state of a chunk in the streaming system.
enum class ChunkState : uint8_t {
    UNLOADED   = 0,
    GENERATING = 1,
    GENERATED  = 2,
    ACTIVE     = 3,
    SLEEPING   = 4,
};

// Display name for each chunk state.
constexpr const char* kChunkStateNames[] = {
    "Unloaded", "Generating", "Generated", "Active", "Sleeping",
};

// Identifies a unique chunk in the voxel world.
struct ChunkKey {
    std::string dimension_id = "overworld";
    int chunk_x = 0;
    int chunk_y = 0;
    int chunk_z = 0;

    ChunkKey() = default;

    ChunkKey(std::string dimension, int x, int y, int z)
        : dimension_id(std::move(dimension))
        , chunk_x(x)
        , chunk_y(y)
        , chunk_z(z) {
    }

    bool operator==(const ChunkKey& other) const {
        return chunk_x == other.chunk_x
            && chunk_y == other.chunk_y
            && chunk_z == other.chunk_z
            && dimension_id == other.dimension_id;
    }

    bool operator!=(const ChunkKey& other) const {
        return !(*this == other);
    }
};

// Placement data for generated world connectors.
struct ConnectorPlacement {
    int64_t connector_id = 0;
    std::string from_dimension;
    int from_cell_x = 0;
    int from_cell_y = 0;
    int from_cell_z = 0;
    std::string to_dimension;
    int to_cell_x = 0;
    int to_cell_y = 0;
    int to_cell_z = 0;
    bool one_way = false;
    bool locked = false;
    std::string connector_type;
    int activation_mode = 0;           // 0 = INTERACT, 1 = AUTO_ON_ENTER
};

// Generated mechanism effect. Kept data-oriented so Godot can instantiate
// presentation resources without owning world-generation rules.
struct MechanismEffectPlacement {
    std::string effect_type;            // "connector_locked"
    int64_t connector_id = 0;
    bool when_active = false;
    bool when_inactive = true;
};

// Placement data for generated world mechanisms.
// Mirrors MapMechanism.gd on the Godot side.
struct MechanismPlacement {
    std::string mechanism_id;
    std::string dimension_id;
    int cell_x = 0;
    int cell_y = 0;
    int cell_z = 0;
    std::string title_key;
    std::string action_label;
    std::string flag_name;
    int activation_mode = 0;            // 0 = INTERACT, 1 = AUTO_ON_ENTER
    bool one_shot = true;
    std::string required_flag;
    std::vector<MechanismEffectPlacement> effects;
};

// World data shard. One chunk represents a fixed-size region of the world.
// This is a pure data container, not a Godot node.
struct ChunkData {
    // Chunk size in cells. Must match the global ChunkConfig.
    static constexpr int kChunkSize = 32;

    int chunk_x = 0;
    int chunk_y = 0;
    int chunk_z = 0;
    ChunkState state = ChunkState::UNLOADED;

    TerrainData terrain;

    // Connector placements.
    std::vector<ConnectorPlacement> connectors;

    // Mechanism placements. Future block entities should replace this.
    std::vector<MechanismPlacement> mechanisms;

    // Entity and machine reference IDs owned by this chunk.
    // Full runtime data lives in the global entity/machine registries.
    std::vector<EntityId> entities;
    std::vector<MachineId> machines;

    // Block entity placements owned by this chunk.
    // Each entry represents one block entity whose root cell is in this chunk.
    std::vector<BlockEntityPlacement> block_entities;

    // Connector runtime IDs (set after ConnectorManager instantiation).
    std::vector<ConnectorId> connector_ids;

    // --- Ecosystem population data ---

    // If true, this chunk has persisted ecosystem population data.
    // When false, EcosystemSystem will initialize defaults on first tick.
    bool has_population_cell = false;

    // Per-chunk ecosystem population density (vegetation, herbivore, predator, etc.).
    // Written by EcosystemSystem before save, restored on load.
    PopulationCell population_cell{};

    // --- Captive creatures (pen / husbandry) ---

    // If true, this chunk has persisted captive creature data.
    bool has_captive_creatures = false;

    // Captive creatures whose capture position is in this chunk.
    // These are persistent individuals detached from the wild population.
    // Written by EcosystemSystem before save, restored on load.
    std::vector<CaptiveCreature> captive_creatures;
};

} // namespace snt::data

// std::hash specialization for ChunkKey to use in unordered_map.
template <>
struct std::hash<snt::data::ChunkKey> {
    size_t operator()(const snt::data::ChunkKey& key) const {
        size_t h = std::hash<std::string>()(key.dimension_id);
        h ^= std::hash<int>()(key.chunk_x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(key.chunk_y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(key.chunk_z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
