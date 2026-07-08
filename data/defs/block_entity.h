// BlockEntity — per-block runtime state attached to a voxel cell.
//
// Ported from src/core/world/block_entity.hpp.
// Namespace: science_and_theology -> snt::data.
// gt sub-namespace merged into snt::data (gt::PipeType -> PipeType, etc.).
//
// Normal terrain cells only store material + flags. When a block needs
// additional runtime state (e.g. tree growth stage, machine inventory,
// furnace progress), a BlockEntity is attached to that cell position.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "defs/entity_data.h"
#include "defs/creature_species.h"
#include "defs/pipe_types.h"
#include "defs/gt_values.h"
#include "defs/crop_species_def.h"

namespace snt::data {

// ============================================================
// BlockEntity — per-block runtime state attached to a voxel cell
// ============================================================
//
// Ownership:
//   - Each BlockEntity is owned by the chunk containing its root cell
//     (the "anchor" position, typically the bottom-most block).
//   - A BlockEntity may reference additional owned cells in the same or
//     neighboring chunks (e.g. a tree's trunk + canopy).
//   - The BlockEntityRegistry provides global lookup by EntityId and
//     spatial lookup by world position.
//
// Serialization:
//   - BlockEntityPlacement is stored in ChunkData for save/load.
//   - Full runtime state is reconstructed from placement data on load.

// Growth stage for tree-type block entities.
enum class TreeGrowthStage : uint8_t {
    SAPLING     = 0,
    YOUNG       = 1,
    MATURE      = 2,
    COUNT       = 3,
};

constexpr const char* kTreeGrowthStageNames[] = {
    "Sapling", "Young", "Mature",
};

// Type discriminator for block entity variants.
enum class BlockEntityType : uint8_t {
    NONE        = 0,
    TREE        = 1,
    MACHINE     = 2,
    CREATURE    = 3,
    PIPE        = 4,
    CABLE       = 5,
    FARMLAND    = 6,   // Tilled farmland (holds moisture/fertility)
    CROP        = 7,   // Crop planted on farmland
    SIGNAL_WIRE = 8,   // Signal wire segment (per-block signal network)
    CUSTOM      = 9,   // Mod-registered block entity type
    COUNT       = 10,
};

constexpr const char* kBlockEntityTypeNames[] = {
    "None", "Tree", "Machine", "Creature", "Pipe", "Cable",
    "Farmland", "Crop", "SignalWire", "Custom",
};

// 6-face connection bitmask used by PIPE and CABLE entities.
// Convention: bit 0 = +X, bit 1 = -X, bit 2 = +Y, bit 3 = -Y,
//             bit 4 = +Z, bit 5 = -Z.
// A set bit means the entity connects to the neighbor on that face.
// (Mirrors the FaceMask ordering used by greedy mesh face culling.)
enum BlockEntityConnectionMask : uint8_t {
    CONN_POS_X = 1u << 0,
    CONN_NEG_X = 1u << 1,
    CONN_POS_Y = 1u << 2,
    CONN_NEG_Y = 1u << 3,
    CONN_POS_Z = 1u << 4,
    CONN_NEG_Z = 1u << 5,
};

// A cell coordinate owned by a BlockEntity.
// Can reference cells in any chunk; the chunk is implied by
// world coordinates (block_x / kChunkSize, etc.).
struct OwnedCell {
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
};

// Lightweight placement data stored in ChunkData for serialization.
// Contains enough information to reconstruct the full runtime entity.
struct BlockEntityPlacement {
    EntityId id;
    BlockEntityType entity_type = BlockEntityType::NONE;

    // Root cell position (world coordinates).
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;

    // Type-specific data encoded as key-value pairs.
    // TREE:     { "species_key": str, "growth_stage": uint8, "planted_tick": int64 }
    // MACHINE:  { "machine_type": str, "facing": uint8, ... }
    // PIPE:     { "pipe_type": uint8, "connections": uint8 }
    // CABLE:    { "cable_tier": uint8, "connections": uint8 }
    // This keeps the placement struct generic and extensible.
    std::string type_data_json;

    // Number of owned cells (for serialization bounds checking).
    uint32_t owned_cell_count = 0;
};

// Full runtime state for a tree block entity.
// Reconstructed from BlockEntityPlacement on chunk load.
struct TreeBlockEntityState {
    std::string species_key;
    TreeGrowthStage growth_stage = TreeGrowthStage::SAPLING;
    int64_t planted_tick = 0;
    int64_t last_growth_tick = 0;
    std::vector<OwnedCell> owned_cells;
};

// ============================================================
// CreatureBlockEntityState — proxy creature entity for ecosystem
// ============================================================
//
// Proxy creatures are visual representations of population density.
// They are spawned in active chunks based on PopulationCell density
// and despawned when the chunk goes to sleep.
//
// AI states:
//   IDLE     — standing still, waiting for next action
//   WANDERING — moving toward a random target within the chunk
//   FLEEING   — running away from a nearby predator (herbivores only)

enum class CreatureState : uint8_t {
    IDLE      = 0,
    WANDERING = 1,
    FLEEING   = 2,
    COUNT     = 3,
};

constexpr const char* kCreatureStateNames[] = {
    "Idle", "Wandering", "Fleeing",
};

struct CreatureBlockEntityState {
    // Species identifier (references CreatureSpeciesRegistry).
    // 0 = invalid, must be set before first tick.
    uint16_t species_id = 0;

    // Cached behavioral role. Set at spawn time from species definition.
    // Avoids per-tick registry lookups in the AI hot path.
    CreatureRole creature_role = CreatureRole::HERBIVORE;

    CreatureState ai_state = CreatureState::IDLE;
    float health = 1.0f;
    int64_t spawn_tick = 0;

    // Current position (world block coordinates).
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float pos_z = 0.0f;

    // Wander target position.
    float wander_target_x = 0.0f;
    float wander_target_y = 0.0f;
    float wander_target_z = 0.0f;
    int64_t next_wander_tick = 0;

    // Flee target: position to run away from.
    float flee_from_x = 0.0f;
    float flee_from_y = 0.0f;
    float flee_from_z = 0.0f;
    int64_t flee_end_tick = 0;
};

// ============================================================
// MachineBlockEntityState — voxel-anchored machine (V28)
// ============================================================

struct MachineBlockEntityState {
    std::string machine_type;        // e.g. "furnace", "coke_oven"
    uint8_t facing = 0;              // 0..5

    // Multiblock formation state.
    bool formed = false;             // is the structure currently formed?
    std::vector<OwnedCell> claimed_cells;  // cells claimed when formed (excl. root)
    std::vector<EntityId> hatch_entities;  // hatch EntityIds when formed
};

// ============================================================
// PipeBlockEntityState — voxel-anchored pipe segment (V28)
// ============================================================

struct PipeBlockEntityState {
    PipeType pipe_type = PipeType::LIQUID;
    uint8_t connections = 0;          // BlockEntityConnectionMask bitmask
    std::vector<OwnedCell> owned_cells;  // usually empty (single cell)
};

// ============================================================
// CableBlockEntityState — voxel-anchored cable segment (V28)
// ============================================================

struct CableBlockEntityState {
    VoltageTier cable_tier = VoltageTier::ULV;
    uint8_t connections = 0;          // BlockEntityConnectionMask bitmask
    std::vector<OwnedCell> owned_cells;  // usually empty (single cell)
};

// ============================================================
// SignalWireBlockEntityState — voxel-anchored signal wire segment
// ============================================================

struct SignalWireBlockEntityState {
    uint8_t connections = 0;          // BlockEntityConnectionMask bitmask
    int32_t signal_strength = 0;      // Cached signal value (0 = unpowered)
    bool is_source = false;           // True if this wire emits signal
    std::vector<OwnedCell> owned_cells;  // usually empty (single cell)
};

// ============================================================
// FarmlandBlockEntityState — tilled soil for crop planting
// ============================================================

struct FarmlandBlockEntityState {
    // Current moisture level [0, 1].
    float moisture = 0.5f;
    // Current fertility level [0, 1].
    float fertility = 0.7f;
    // Last planted crop species key (for rotation penalty). Empty = none.
    std::string last_crop_key;
    // Consecutive plantings of the same crop (>=3 triggers penalty).
    int consecutive_same_crop = 0;
    // Last tick moisture was updated (for evaporation timing).
    int64_t last_moisture_tick = 0;
};

// ============================================================
// CropBlockEntityState — growing crop on farmland
// ============================================================

struct CropBlockEntityState {
    std::string species_key;
    CropGrowthStage growth_stage = CropGrowthStage::SEED;
    int64_t planted_tick = 0;
    int64_t last_growth_tick = 0;
    // Regrow timer for repeat-harvest crops.
    int64_t last_harvest_tick = 0;
    // Cells occupied by the crop (usually 1, large crops may use more).
    std::vector<OwnedCell> owned_cells;
};

// ============================================================
// CustomBlockEntityState — runtime state for mod-registered entities
// ============================================================

struct CustomBlockEntityState {
    std::string type_key;        // e.g. "my_mod:custom_furnace"
    std::string state_json;      // opaque mod-controlled state blob
    std::vector<OwnedCell> owned_cells;
};

} // namespace snt::data
