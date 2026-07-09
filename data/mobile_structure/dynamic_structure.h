#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../defs/terrain_data.h"
#include "../world_gen/world_gen_config.h"

namespace snt::data {

class ChunkRegistry;



using DynamicStructureId = uint64_t;
inline constexpr DynamicStructureId kInvalidDynamicStructureId = 0;

// Integer world/local block coordinate.
struct BlockPos3i {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const BlockPos3i& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct BlockPos3iHash {
    size_t operator()(const BlockPos3i& p) const;
};

// One block inside a dynamic structure snapshot. Local coordinates are relative
// to the assembly anchor so the whole structure can move by changing transform.
struct LocalStructureBlock {
    int32_t local_x = 0;
    int32_t local_y = 0;
    int32_t local_z = 0;
    TerrainMaterialId material = 0;
    uint32_t flags = 0;
};

// Sparse local block collection. This is intentionally palette-free for the
// first core implementation. Rendering/network layers can build palettes from
// this snapshot without changing the authoritative representation.
struct DynamicStructureSnapshot {
    std::vector<LocalStructureBlock> blocks;

    int32_t min_x = 0;
    int32_t min_y = 0;
    int32_t min_z = 0;
    int32_t max_x = 0;
    int32_t max_y = 0;
    int32_t max_z = 0;

    bool empty() const { return blocks.empty(); }
    size_t block_count() const { return blocks.size(); }
};

// Authoritative transform for a mobile structure. Movement sync should send
// this transform, not per-block terrain deltas.
struct DynamicStructureTransform {
    std::string dimension_id = "overworld";

    double position_x = 0.0;
    double position_y = 0.0;
    double position_z = 0.0;

    // Unit quaternion. Identity by default.
    float rotation_x = 0.0f;
    float rotation_y = 0.0f;
    float rotation_z = 0.0f;
    float rotation_w = 1.0f;

    double velocity_x = 0.0;
    double velocity_y = 0.0;
    double velocity_z = 0.0;

    double angular_velocity_x = 0.0;
    double angular_velocity_y = 0.0;
    double angular_velocity_z = 0.0;
};

// Small network-facing payload. Clients can interpolate these snapshots while
// reusing the last structure snapshot identified by structure_version.
struct DynamicStructureTransformSnapshot {
    DynamicStructureId structure_id = kInvalidDynamicStructureId;
    uint32_t structure_version = 0;
    int64_t tick = 0;
    DynamicStructureTransform transform;
};

struct DynamicStructureEntity {
    DynamicStructureId id = kInvalidDynamicStructureId;
    uint32_t structure_version = 1;
    DynamicStructureTransform transform;
    DynamicStructureSnapshot snapshot;

    // Coarse runtime values. Mass is currently the block count; later this can
    // be material-density weighted without changing assembly/disassembly APIs.
    double mass = 0.0;
    bool moving = false;
    bool dirty_mesh = true;
};

// Terrain cell mutation emitted by assembly/disassembly. Higher layers should
// use this to emit terrain_changed events and/or batch network deltas.
struct MobileTerrainDelta {
    std::string dimension_id;
    int32_t chunk_x = 0;
    int32_t chunk_y = 0;
    int32_t chunk_z = 0;
    int32_t local_x = 0;
    int32_t local_y = 0;
    int32_t local_z = 0;
    TerrainMaterialId old_material = 0;
    TerrainMaterialId new_material = 0;
};

struct AssembleOptions {
    // Hard cap for flood-fill assembly. Prevents a ship core from accidentally
    // collecting a whole planet or a giant base.
    size_t max_blocks = 4096;

    // When true, source world terrain cells are cleared to air after the
    // snapshot has been collected. This is the moving-contraption mode.
    bool clear_source_cells = true;

    // If true, encountering a missing neighbor chunk aborts the assembly.
    // If false, missing chunks simply act as flood-fill boundaries.
    bool fail_on_missing_chunks = false;
};

struct AssembleResult {
    bool success = false;
    bool hit_block_limit = false;
    std::string error;
    DynamicStructureId structure_id = kInvalidDynamicStructureId;
    size_t block_count = 0;
    std::vector<MobileTerrainDelta> terrain_deltas;
};

struct DisassembleOptions {
    // If true, removes the dynamic entity from the registry after writing it
    // back to world terrain.
    bool remove_entity = true;

    // If true, disassembly fails if any destination cell is not air.
    // If false, destination cells are overwritten. Keep true for ship docking.
    bool require_air_destination = true;
};

struct DisassembleResult {
    bool success = false;
    std::string error;
    size_t block_count = 0;
    std::vector<MobileTerrainDelta> terrain_deltas;
};

// Stores dynamic structures as entities. This is the server-side authoritative
// registry; clients should receive snapshots/deltas derived from it.
class DynamicStructureRegistry {
public:
    DynamicStructureId add(DynamicStructureEntity entity);
    DynamicStructureEntity* get(DynamicStructureId id);
    const DynamicStructureEntity* get(DynamicStructureId id) const;
    bool remove(DynamicStructureId id);
    size_t count() const { return structures_.size(); }
    void clear();

private:
    DynamicStructureId next_id_ = 1;
    std::unordered_map<DynamicStructureId, DynamicStructureEntity> structures_;
};

// Converts connected world terrain cells to/from DynamicStructureEntity.
class DynamicStructureAssembler {
public:
    using IncludePredicate = std::function<bool(const TerrainCell&)>;

    static AssembleResult assemble_connected(
        ChunkRegistry& chunks,
        DynamicStructureRegistry& registry,
        const WorldGenConfigSnapshot* worldgen_config,
        const std::string& dimension_id,
        int32_t seed_x, int32_t seed_y, int32_t seed_z,
        IncludePredicate include,
        const AssembleOptions& options = AssembleOptions{});

    static DisassembleResult disassemble_to_world(
        ChunkRegistry& chunks,
        DynamicStructureRegistry& registry,
        const WorldGenConfigSnapshot* worldgen_config,
        DynamicStructureId structure_id,
        const DisassembleOptions& options = DisassembleOptions{});

    static DynamicStructureTransformSnapshot make_transform_snapshot(
        const DynamicStructureEntity& entity,
        int64_t tick);
};


} // namespace snt::data
