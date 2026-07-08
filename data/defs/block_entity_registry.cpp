// BlockEntityRegistry implementation.
//
// Ported from src/core/world/block_entity_registry.cpp.
// Namespace: science_and_theology -> snt::data.
// gt sub-namespace merged into snt::data (gt::PipeType -> PipeType, etc.).

#include "defs/block_entity_registry.h"
#include "defs/chunk_data.h"

#include <algorithm>
#include <cmath>

namespace snt::data {

// --- Entity ID generation ---

EntityId BlockEntityRegistry::next_id() {
    EntityId id{next_id_};
    ++next_id_;
    return id;
}

// --- Registration ---

EntityId BlockEntityRegistry::register_tree_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    const std::string& species_key,
    TreeGrowthStage growth_stage,
    int64_t planted_tick,
    const std::vector<OwnedCell>& owned_cells) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::TREE;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count =
        static_cast<uint32_t>(owned_cells.size());

    // Encode type-specific data as a simple structured string.
    // Format: "species_key|growth_stage|planted_tick"
    entry.placement.type_data_json =
        species_key + "|" +
        std::to_string(static_cast<int>(growth_stage)) + "|" +
        std::to_string(planted_tick);

    entry.tree_state.species_key = species_key;
    entry.tree_state.growth_stage = growth_stage;
    entry.tree_state.planted_tick = planted_tick;
    entry.tree_state.last_growth_tick = planted_tick;
    entry.tree_state.owned_cells = owned_cells;

    // Index the owned cells for spatial lookup.
    index_owned_cells(id, owned_cells);

    // Index by chunk.
    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    entities_[id] = std::move(entry);
    return id;
}

EntityId BlockEntityRegistry::register_creature_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    uint16_t species_id,
    CreatureRole role,
    int64_t spawn_tick) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::CREATURE;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count = 0;

    entry.creature_state.species_id = species_id;
    entry.creature_state.creature_role = role;
    entry.creature_state.health = 1.0f;
    entry.creature_state.spawn_tick = spawn_tick;

    // Index by chunk.
    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    entities_[id] = std::move(entry);
    return id;
}

EntityId BlockEntityRegistry::register_machine_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    const std::string& machine_type,
    uint8_t facing) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::MACHINE;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count = 0;

    // Encode as "machine_type|facing".
    entry.placement.type_data_json =
        machine_type + "|" + std::to_string(static_cast<int>(facing));

    entry.machine_state.machine_type = machine_type;
    entry.machine_state.facing = facing;
    entry.machine_state.formed = false;  // unformed until check_machine_formation

    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    entities_[id] = std::move(entry);
    return id;
}

EntityId BlockEntityRegistry::register_pipe_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    PipeType pipe_type,
    uint8_t connections) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::PIPE;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count = 0;

    // Encode as "pipe_type|connections".
    entry.placement.type_data_json =
        std::to_string(static_cast<int>(pipe_type)) + "|" +
        std::to_string(static_cast<int>(connections));

    entry.pipe_state.pipe_type = pipe_type;
    entry.pipe_state.connections = connections;

    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    entities_[id] = std::move(entry);
    return id;
}

EntityId BlockEntityRegistry::register_cable_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    VoltageTier cable_tier,
    uint8_t connections) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::CABLE;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count = 0;

    // Encode as "cable_tier|connections".
    entry.placement.type_data_json =
        std::to_string(static_cast<int>(cable_tier)) + "|" +
        std::to_string(static_cast<int>(connections));

    entry.cable_state.cable_tier = cable_tier;
    entry.cable_state.connections = connections;

    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    // Cables are single-cell entities; register the root cell so
    // find_owner_at() can locate them for neighbor connection checks.
    CellKey root_cell{root_x, root_y, root_z};
    cell_owners_[root_cell] = id;

    entities_[id] = std::move(entry);
    return id;
}

EntityId BlockEntityRegistry::register_farmland_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    float initial_moisture, float initial_fertility,
    int64_t current_tick) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::FARMLAND;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count = 0;

    // Encode type-specific data: "moisture|fertility|last_crop_key|consecutive"
    entry.placement.type_data_json =
        std::to_string(initial_moisture) + "|" +
        std::to_string(initial_fertility) + "||0";

    entry.farmland_state.moisture = initial_moisture;
    entry.farmland_state.fertility = initial_fertility;
    entry.farmland_state.last_crop_key = "";
    entry.farmland_state.consecutive_same_crop = 0;
    entry.farmland_state.last_moisture_tick = current_tick;

    // Index by chunk.
    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    entities_[id] = std::move(entry);
    return id;
}

EntityId BlockEntityRegistry::register_crop_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    const std::string& species_key,
    CropGrowthStage growth_stage,
    int64_t planted_tick,
    const std::vector<OwnedCell>& owned_cells) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::CROP;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count =
        static_cast<uint32_t>(owned_cells.size());

    // Encode type-specific data: "species_key|growth_stage|planted_tick"
    entry.placement.type_data_json =
        species_key + "|" +
        std::to_string(static_cast<int>(growth_stage)) + "|" +
        std::to_string(planted_tick);

    entry.crop_state.species_key = species_key;
    entry.crop_state.growth_stage = growth_stage;
    entry.crop_state.planted_tick = planted_tick;
    entry.crop_state.last_growth_tick = planted_tick;
    entry.crop_state.last_harvest_tick = 0;
    entry.crop_state.owned_cells = owned_cells;

    // Index the owned cells for spatial lookup.
    index_owned_cells(id, owned_cells);

    // Index by chunk.
    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    entities_[id] = std::move(entry);
    return id;
}

EntityId BlockEntityRegistry::register_signal_wire_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    uint8_t connections,
    bool is_source) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::SIGNAL_WIRE;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count = 0;

    // Encode as "connections|is_source".
    entry.placement.type_data_json =
        std::to_string(static_cast<int>(connections)) + "|" +
        std::to_string(is_source ? 1 : 0);

    entry.signal_wire_state.connections = connections;
    entry.signal_wire_state.is_source = is_source;
    entry.signal_wire_state.signal_strength = 0;

    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    // Signal wires are single-cell entities; register the root cell so
    // find_owner_at() can locate them for neighbor connection checks.
    CellKey root_cell{root_x, root_y, root_z};
    cell_owners_[root_cell] = id;

    entities_[id] = std::move(entry);
    return id;
}

EntityId BlockEntityRegistry::register_custom_entity(
    const std::string& dimension_id,
    int32_t root_x, int32_t root_y, int32_t root_z,
    const std::string& type_key,
    const std::string& initial_state_json,
    const std::vector<OwnedCell>& owned_cells) {
    EntityId id = next_id();

    BlockEntityEntry entry;
    entry.dimension_id = dimension_id;
    entry.placement.id = id;
    entry.placement.entity_type = BlockEntityType::CUSTOM;
    entry.placement.root_x = root_x;
    entry.placement.root_y = root_y;
    entry.placement.root_z = root_z;
    entry.placement.owned_cell_count =
        static_cast<uint32_t>(owned_cells.size());

    // Encode as "type_key|state_json".
    entry.placement.type_data_json = type_key + "|" + initial_state_json;

    entry.custom_state.type_key = type_key;
    entry.custom_state.state_json = initial_state_json;
    entry.custom_state.owned_cells = owned_cells;

    ChunkRefKey ck = chunk_for_block(dimension_id, root_x, root_y, root_z);
    chunk_entities_[ck].push_back(id);

    // Index owned cells (root cell included if not separately listed).
    index_owned_cells(id, owned_cells);
    bool root_listed = false;
    for (const auto& c : owned_cells) {
        if (c.block_x == root_x && c.block_y == root_y && c.block_z == root_z) {
            root_listed = true;
            break;
        }
    }
    if (!root_listed) {
        CellKey root_cell{root_x, root_y, root_z};
        cell_owners_[root_cell] = id;
    }

    entities_[id] = std::move(entry);
    return id;
}

void BlockEntityRegistry::remove_entity(EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;

    BlockEntityEntry& entry = it->second;

    // Remove spatial index entries.
    if (entry.placement.entity_type == BlockEntityType::TREE) {
        unindex_owned_cells(entry.tree_state.owned_cells);
    } else if (entry.placement.entity_type == BlockEntityType::CROP) {
        unindex_owned_cells(entry.crop_state.owned_cells);
    } else if (entry.placement.entity_type == BlockEntityType::MACHINE) {
        // Only formed machines have indexed claimed cells.
        unindex_owned_cells(entry.machine_state.claimed_cells);
    } else if (entry.placement.entity_type == BlockEntityType::SIGNAL_WIRE ||
               entry.placement.entity_type == BlockEntityType::CABLE) {
        // Single-cell entities register their root cell in cell_owners_.
        CellKey root_cell{entry.placement.root_x,
                          entry.placement.root_y,
                          entry.placement.root_z};
        cell_owners_.erase(root_cell);
    }

    // Remove from chunk index.
    ChunkRefKey ck = chunk_for_block(
        entry.dimension_id,
        entry.placement.root_x,
        entry.placement.root_y,
        entry.placement.root_z);
    auto chunk_it = chunk_entities_.find(ck);
    if (chunk_it != chunk_entities_.end()) {
        auto& ids = chunk_it->second;
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        if (ids.empty()) {
            chunk_entities_.erase(chunk_it);
        }
    }

    entities_.erase(it);
}

void BlockEntityRegistry::remove_entities_in_chunk(
    const std::string& dimension_id,
    int chunk_x, int chunk_y, int chunk_z) {
    ChunkRefKey ck{dimension_id, chunk_x, chunk_y, chunk_z};
    auto it = chunk_entities_.find(ck);
    if (it == chunk_entities_.end()) return;

    // Copy the IDs since remove_entity modifies chunk_entities_.
    auto ids = it->second;
    for (const auto& id : ids) {
        remove_entity(id);
    }
}

// --- Query by EntityId ---

BlockEntityType BlockEntityRegistry::get_entity_type(EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return BlockEntityType::NONE;
    return it->second.placement.entity_type;
}

const TreeBlockEntityState* BlockEntityRegistry::get_tree_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::TREE) return nullptr;
    return &it->second.tree_state;
}

TreeBlockEntityState* BlockEntityRegistry::get_tree_state_mut(EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::TREE) return nullptr;
    return &it->second.tree_state;
}

const CreatureBlockEntityState* BlockEntityRegistry::get_creature_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::CREATURE) return nullptr;
    return &it->second.creature_state;
}

CreatureBlockEntityState* BlockEntityRegistry::get_creature_state_mut(
    EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::CREATURE) return nullptr;
    return &it->second.creature_state;
}

const MachineBlockEntityState* BlockEntityRegistry::get_machine_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::MACHINE) return nullptr;
    return &it->second.machine_state;
}

MachineBlockEntityState* BlockEntityRegistry::get_machine_state_mut(
    EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::MACHINE) return nullptr;
    return &it->second.machine_state;
}

const PipeBlockEntityState* BlockEntityRegistry::get_pipe_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::PIPE) return nullptr;
    return &it->second.pipe_state;
}

PipeBlockEntityState* BlockEntityRegistry::get_pipe_state_mut(
    EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::PIPE) return nullptr;
    return &it->second.pipe_state;
}

const CableBlockEntityState* BlockEntityRegistry::get_cable_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::CABLE) return nullptr;
    return &it->second.cable_state;
}

CableBlockEntityState* BlockEntityRegistry::get_cable_state_mut(
    EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::CABLE) return nullptr;
    return &it->second.cable_state;
}

const FarmlandBlockEntityState* BlockEntityRegistry::get_farmland_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::FARMLAND) return nullptr;
    return &it->second.farmland_state;
}

FarmlandBlockEntityState* BlockEntityRegistry::get_farmland_state_mut(
    EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::FARMLAND) return nullptr;
    return &it->second.farmland_state;
}

const CropBlockEntityState* BlockEntityRegistry::get_crop_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::CROP) return nullptr;
    return &it->second.crop_state;
}

CropBlockEntityState* BlockEntityRegistry::get_crop_state_mut(
    EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::CROP) return nullptr;
    return &it->second.crop_state;
}

const SignalWireBlockEntityState* BlockEntityRegistry::get_signal_wire_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::SIGNAL_WIRE) return nullptr;
    return &it->second.signal_wire_state;
}

SignalWireBlockEntityState* BlockEntityRegistry::get_signal_wire_state_mut(
    EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::SIGNAL_WIRE) return nullptr;
    return &it->second.signal_wire_state;
}

const CustomBlockEntityState* BlockEntityRegistry::get_custom_state(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::CUSTOM) return nullptr;
    return &it->second.custom_state;
}

CustomBlockEntityState* BlockEntityRegistry::get_custom_state_mut(
    EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.placement.entity_type != BlockEntityType::CUSTOM) return nullptr;
    return &it->second.custom_state;
}

const BlockEntityPlacement* BlockEntityRegistry::get_placement(
    EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    return &it->second.placement;
}

const std::string* BlockEntityRegistry::get_dimension_id(EntityId id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    return &it->second.dimension_id;
}

// --- Spatial query ---

EntityId BlockEntityRegistry::find_owner_at(
    int32_t block_x, int32_t block_y, int32_t block_z) const {
    CellKey key{block_x, block_y, block_z};
    auto it = cell_owners_.find(key);
    if (it == cell_owners_.end()) return EntityId{};
    return it->second;
}

std::vector<EntityId> BlockEntityRegistry::entities_in_chunk(
    const std::string& dimension_id,
    int chunk_x, int chunk_y, int chunk_z) const {
    ChunkRefKey ck{dimension_id, chunk_x, chunk_y, chunk_z};
    auto it = chunk_entities_.find(ck);
    if (it == chunk_entities_.end()) return {};
    return it->second;
}

// --- Owned cell management ---

void BlockEntityRegistry::update_tree_owned_cells(
    EntityId id, const std::vector<OwnedCell>& new_cells) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    if (it->second.placement.entity_type != BlockEntityType::TREE) return;

    // Remove old spatial index entries.
    unindex_owned_cells(it->second.tree_state.owned_cells);

    // Update owned cells.
    it->second.tree_state.owned_cells = new_cells;
    it->second.placement.owned_cell_count =
        static_cast<uint32_t>(new_cells.size());

    // Add new spatial index entries.
    index_owned_cells(id, new_cells);
}

void BlockEntityRegistry::set_machine_formation(
    EntityId id,
    bool formed,
    const std::vector<OwnedCell>& claimed_cells,
    const std::vector<EntityId>& hatch_entities) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    if (it->second.placement.entity_type != BlockEntityType::MACHINE) return;

    // Remove old claimed-cell spatial index entries.
    unindex_owned_cells(it->second.machine_state.claimed_cells);

    // Update formation state.
    it->second.machine_state.formed = formed;
    it->second.machine_state.claimed_cells = claimed_cells;
    it->second.machine_state.hatch_entities = hatch_entities;

    // Add new claimed-cell spatial index entries.
    index_owned_cells(id, claimed_cells);

    // Update owned_cell_count for serialization.
    it->second.placement.owned_cell_count =
        static_cast<uint32_t>(claimed_cells.size());
}

void BlockEntityRegistry::update_crop_owned_cells(
    EntityId id, const std::vector<OwnedCell>& new_cells) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    if (it->second.placement.entity_type != BlockEntityType::CROP) return;

    unindex_owned_cells(it->second.crop_state.owned_cells);
    it->second.crop_state.owned_cells = new_cells;
    it->second.placement.owned_cell_count =
        static_cast<uint32_t>(new_cells.size());
    index_owned_cells(id, new_cells);
}

void BlockEntityRegistry::update_pipe_connections(EntityId id, uint8_t connections) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    if (it->second.placement.entity_type != BlockEntityType::PIPE) return;
    it->second.pipe_state.connections = connections;
}

void BlockEntityRegistry::update_cable_connections(EntityId id, uint8_t connections) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    if (it->second.placement.entity_type != BlockEntityType::CABLE) return;
    it->second.cable_state.connections = connections;
}

void BlockEntityRegistry::update_signal_wire_connections(EntityId id, uint8_t connections) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    if (it->second.placement.entity_type != BlockEntityType::SIGNAL_WIRE) return;
    it->second.signal_wire_state.connections = connections;
}

void BlockEntityRegistry::update_signal_wire_strength(EntityId id, int32_t strength) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    if (it->second.placement.entity_type != BlockEntityType::SIGNAL_WIRE) return;
    it->second.signal_wire_state.signal_strength = strength;
}

void BlockEntityRegistry::update_signal_wire_source(EntityId id, bool is_source) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return;
    if (it->second.placement.entity_type != BlockEntityType::SIGNAL_WIRE) return;
    it->second.signal_wire_state.is_source = is_source;
}

// --- Iteration ---

void BlockEntityRegistry::for_each_tree(
    std::function<void(EntityId, const TreeBlockEntityState&)> fn) const {
    for (const auto& [id, entry] : entities_) {
        if (entry.placement.entity_type == BlockEntityType::TREE) {
            fn(id, entry.tree_state);
        }
    }
}

void BlockEntityRegistry::for_each_machine(
    std::function<void(EntityId, const MachineBlockEntityState&)> fn) const {
    for (const auto& [id, entry] : entities_) {
        if (entry.placement.entity_type == BlockEntityType::MACHINE) {
            fn(id, entry.machine_state);
        }
    }
}

void BlockEntityRegistry::for_each_pipe(
    std::function<void(EntityId, const PipeBlockEntityState&)> fn) const {
    for (const auto& [id, entry] : entities_) {
        if (entry.placement.entity_type == BlockEntityType::PIPE) {
            fn(id, entry.pipe_state);
        }
    }
}

void BlockEntityRegistry::for_each_cable(
    std::function<void(EntityId, const CableBlockEntityState&)> fn) const {
    for (const auto& [id, entry] : entities_) {
        if (entry.placement.entity_type == BlockEntityType::CABLE) {
            fn(id, entry.cable_state);
        }
    }
}

void BlockEntityRegistry::for_each_crop(
    std::function<void(EntityId, const CropBlockEntityState&)> fn) const {
    for (const auto& [id, entry] : entities_) {
        if (entry.placement.entity_type == BlockEntityType::CROP) {
            fn(id, entry.crop_state);
        }
    }
}

void BlockEntityRegistry::for_each_farmland(
    std::function<void(EntityId, const FarmlandBlockEntityState&)> fn) const {
    for (const auto& [id, entry] : entities_) {
        if (entry.placement.entity_type == BlockEntityType::FARMLAND) {
            fn(id, entry.farmland_state);
        }
    }
}

void BlockEntityRegistry::for_each_signal_wire(
    std::function<void(EntityId, const SignalWireBlockEntityState&)> fn) const {
    for (const auto& [id, entry] : entities_) {
        if (entry.placement.entity_type == BlockEntityType::SIGNAL_WIRE) {
            fn(id, entry.signal_wire_state);
        }
    }
}

void BlockEntityRegistry::for_each_custom(
    std::function<void(EntityId, const CustomBlockEntityState&)> fn) const {
    for (const auto& [id, entry] : entities_) {
        if (entry.placement.entity_type == BlockEntityType::CUSTOM) {
            fn(id, entry.custom_state);
        }
    }
}

// --- Private helpers ---

void BlockEntityRegistry::index_owned_cells(
    EntityId id, const std::vector<OwnedCell>& cells) {
    for (const auto& cell : cells) {
        CellKey key{cell.block_x, cell.block_y, cell.block_z};
        cell_owners_[key] = id;
    }
}

void BlockEntityRegistry::unindex_owned_cells(
    const std::vector<OwnedCell>& cells) {
    for (const auto& cell : cells) {
        CellKey key{cell.block_x, cell.block_y, cell.block_z};
        cell_owners_.erase(key);
    }
}

BlockEntityRegistry::ChunkRefKey BlockEntityRegistry::chunk_for_block(
    const std::string& dimension_id,
    int32_t block_x, int32_t block_y, int32_t block_z) {
    return ChunkRefKey{
        dimension_id,
        block_x / ChunkData::kChunkSize,
        block_y / ChunkData::kChunkSize,
        block_z / ChunkData::kChunkSize
    };
}

void BlockEntityRegistry::clear() {
    entities_.clear();
    cell_owners_.clear();
    chunk_entities_.clear();
    next_id_ = 1;
}

} // namespace snt::data
