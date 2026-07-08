// BlockEntityRegistry — global registry for all block entities.
//
// Ported from src/core/world/block_entity_registry.hpp.
// Namespace: science_and_theology -> snt::data.
// gt sub-namespace merged into snt::data (gt::PipeType -> PipeType, etc.).
//
// Provides:
//   - O(1) lookup by EntityId
//   - Spatial lookup by world position (which entity owns a cell?)
//   - Per-chunk iteration (entities whose root is in a given chunk)
//
// Thread safety: main thread only. Not safe for concurrent access.
// The registry is owned by WorldData and accessed by simulation systems.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

#include "defs/entity_data.h"
#include "defs/block_entity.h"

namespace snt::data {

// ============================================================
// BlockEntityRegistry — global registry for all block entities
// ============================================================

class BlockEntityRegistry {
public:
    // Internal storage for a block entity with its runtime state.
    // Defined first so that all_entities() can reference it.
    struct BlockEntityEntry {
        BlockEntityPlacement placement;
        std::string dimension_id;

        // Type-specific runtime state.
        // Only one variant is active at a time, determined by entity_type.
        TreeBlockEntityState tree_state;
        CreatureBlockEntityState creature_state;
        MachineBlockEntityState machine_state;
        PipeBlockEntityState pipe_state;
        CableBlockEntityState cable_state;
        FarmlandBlockEntityState farmland_state;
        CropBlockEntityState crop_state;
        SignalWireBlockEntityState signal_wire_state;
        CustomBlockEntityState custom_state;
    };

    BlockEntityRegistry() = default;
    ~BlockEntityRegistry() = default;

    // Disallow copy.
    BlockEntityRegistry(const BlockEntityRegistry&) = delete;
    BlockEntityRegistry& operator=(const BlockEntityRegistry&) = delete;

    // --- Entity ID generation ---

    // Generate the next unique EntityId for a block entity.
    EntityId next_id();

    // --- Registration ---

    EntityId register_tree_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        const std::string& species_key,
        TreeGrowthStage growth_stage,
        int64_t planted_tick,
        const std::vector<OwnedCell>& owned_cells);

    EntityId register_creature_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        uint16_t species_id,
        CreatureRole role,
        int64_t spawn_tick);

    EntityId register_machine_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        const std::string& machine_type,
        uint8_t facing);

    EntityId register_pipe_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        PipeType pipe_type,
        uint8_t connections);

    EntityId register_cable_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        VoltageTier cable_tier,
        uint8_t connections);

    EntityId register_farmland_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        float initial_moisture, float initial_fertility,
        int64_t current_tick);

    EntityId register_crop_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        const std::string& species_key,
        CropGrowthStage growth_stage,
        int64_t planted_tick,
        const std::vector<OwnedCell>& owned_cells);

    EntityId register_signal_wire_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        uint8_t connections,
        bool is_source);

    EntityId register_custom_entity(
        const std::string& dimension_id,
        int32_t root_x, int32_t root_y, int32_t root_z,
        const std::string& type_key,
        const std::string& initial_state_json,
        const std::vector<OwnedCell>& owned_cells);

    // Remove a block entity by ID. Also removes its spatial index entries.
    void remove_entity(EntityId id);

    // Remove all entities whose root is in the given chunk.
    void remove_entities_in_chunk(
        const std::string& dimension_id,
        int chunk_x, int chunk_y, int chunk_z);

    // --- Query by EntityId ---

    BlockEntityType get_entity_type(EntityId id) const;

    const TreeBlockEntityState* get_tree_state(EntityId id) const;
    TreeBlockEntityState* get_tree_state_mut(EntityId id);

    const CreatureBlockEntityState* get_creature_state(EntityId id) const;
    CreatureBlockEntityState* get_creature_state_mut(EntityId id);

    const MachineBlockEntityState* get_machine_state(EntityId id) const;
    MachineBlockEntityState* get_machine_state_mut(EntityId id);

    const PipeBlockEntityState* get_pipe_state(EntityId id) const;
    PipeBlockEntityState* get_pipe_state_mut(EntityId id);

    const CableBlockEntityState* get_cable_state(EntityId id) const;
    CableBlockEntityState* get_cable_state_mut(EntityId id);

    const FarmlandBlockEntityState* get_farmland_state(EntityId id) const;
    FarmlandBlockEntityState* get_farmland_state_mut(EntityId id);

    const CropBlockEntityState* get_crop_state(EntityId id) const;
    CropBlockEntityState* get_crop_state_mut(EntityId id);

    const SignalWireBlockEntityState* get_signal_wire_state(EntityId id) const;
    SignalWireBlockEntityState* get_signal_wire_state_mut(EntityId id);

    const CustomBlockEntityState* get_custom_state(EntityId id) const;
    CustomBlockEntityState* get_custom_state_mut(EntityId id);

    const BlockEntityPlacement* get_placement(EntityId id) const;

    const std::string* get_dimension_id(EntityId id) const;

    // --- Spatial query ---

    EntityId find_owner_at(int32_t block_x, int32_t block_y, int32_t block_z) const;

    // Returns the MACHINE entity whose root/controller is exactly at the cell.
    EntityId find_machine_root_at(int32_t block_x, int32_t block_y, int32_t block_z) const {
        for (const auto& pair : entities_) {
            const BlockEntityEntry& entry = pair.second;
            if (entry.placement.entity_type != BlockEntityType::MACHINE) continue;
            if (entry.placement.root_x == block_x &&
                entry.placement.root_y == block_y &&
                entry.placement.root_z == block_z) {
                return pair.first;
            }
        }
        return EntityId{};
    }

    std::vector<EntityId> entities_in_chunk(
        const std::string& dimension_id,
        int chunk_x, int chunk_y, int chunk_z) const;

    const std::unordered_map<EntityId, BlockEntityEntry>& all_entities() const {
        return entities_;
    }

    // --- Owned cell management ---

    void update_tree_owned_cells(
        EntityId id, const std::vector<OwnedCell>& new_cells);

    void set_machine_formation(
        EntityId id,
        bool formed,
        const std::vector<OwnedCell>& claimed_cells,
        const std::vector<EntityId>& hatch_entities);

    void update_crop_owned_cells(
        EntityId id, const std::vector<OwnedCell>& new_cells);

    void update_pipe_connections(EntityId id, uint8_t connections);
    void update_cable_connections(EntityId id, uint8_t connections);
    void update_signal_wire_connections(EntityId id, uint8_t connections);
    void update_signal_wire_strength(EntityId id, int32_t strength);
    void update_signal_wire_source(EntityId id, bool is_source);

    // --- Iteration ---

    void for_each_tree(
        std::function<void(EntityId, const TreeBlockEntityState&)> fn) const;
    void for_each_machine(
        std::function<void(EntityId, const MachineBlockEntityState&)> fn) const;
    void for_each_pipe(
        std::function<void(EntityId, const PipeBlockEntityState&)> fn) const;
    void for_each_cable(
        std::function<void(EntityId, const CableBlockEntityState&)> fn) const;
    void for_each_crop(
        std::function<void(EntityId, const CropBlockEntityState&)> fn) const;
    void for_each_farmland(
        std::function<void(EntityId, const FarmlandBlockEntityState&)> fn) const;
    void for_each_signal_wire(
        std::function<void(EntityId, const SignalWireBlockEntityState&)> fn) const;
    void for_each_custom(
        std::function<void(EntityId, const CustomBlockEntityState&)> fn) const;

    size_t size() const { return entities_.size(); }

    // Remove all entities and reset the ID counter.
    void clear();

private:
    // Spatial index key for a cell position.
    struct CellKey {
        int32_t x, y, z;

        bool operator==(const CellKey& o) const {
            return x == o.x && y == o.y && z == o.z;
        }
    };

    struct CellKeyHash {
        size_t operator()(const CellKey& k) const {
            size_t h = static_cast<size_t>(k.x) * 73856093ULL;
            h ^= static_cast<size_t>(k.y) * 19349663ULL;
            h ^= static_cast<size_t>(k.z) * 83492791ULL;
            return h;
        }
    };

    // Chunk key for per-chunk entity grouping.
    struct ChunkRefKey {
        std::string dimension_id;
        int chunk_x, chunk_y, chunk_z;

        bool operator==(const ChunkRefKey& o) const {
            return dimension_id == o.dimension_id
                && chunk_x == o.chunk_x
                && chunk_y == o.chunk_y
                && chunk_z == o.chunk_z;
        }
    };

    struct ChunkRefKeyHash {
        size_t operator()(const ChunkRefKey& k) const {
            size_t h = std::hash<std::string>()(k.dimension_id);
            h ^= static_cast<size_t>(k.chunk_x) * 73856093ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(k.chunk_y) * 19349663ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(k.chunk_z) * 83492791ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // Add spatial index entries for a set of owned cells.
    void index_owned_cells(EntityId id, const std::vector<OwnedCell>& cells);

    // Remove spatial index entries for a set of owned cells.
    void unindex_owned_cells(const std::vector<OwnedCell>& cells);

    // Compute chunk coordinates from a world block position.
    static ChunkRefKey chunk_for_block(
        const std::string& dimension_id,
        int32_t block_x, int32_t block_y, int32_t block_z);

    // Entity storage: EntityId -> entry.
    std::unordered_map<EntityId, BlockEntityEntry> entities_;

    // Spatial index: cell position -> owning EntityId.
    std::unordered_map<CellKey, EntityId, CellKeyHash> cell_owners_;

    // Per-chunk index: chunk key -> entity IDs rooted in that chunk.
    std::unordered_map<ChunkRefKey, std::vector<EntityId>, ChunkRefKeyHash> chunk_entities_;

    // Monotonic ID counter.
    uint64_t next_id_ = 1;
};

} // namespace snt::data
