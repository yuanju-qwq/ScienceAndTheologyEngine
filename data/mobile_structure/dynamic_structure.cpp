#include "dynamic_structure.h"

#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>
#include <utility>

#include "../defs/chunk_data.h"
#include "../world/chunk_registry.h"
#include "../world_gen/world_gen_config.h"

namespace snt::data {

namespace {

int floor_div_chunk(int value) {
    return static_cast<int>(
        std::floor(static_cast<float>(value) / ChunkData::kChunkSize));
}

struct ResolvedCell {
    ChunkData* chunk = nullptr;
    TerrainCell* cell = nullptr;
    int32_t chunk_x = 0;
    int32_t chunk_y = 0;
    int32_t chunk_z = 0;
    int32_t local_x = 0;
    int32_t local_y = 0;
    int32_t local_z = 0;
};

ResolvedCell resolve_cell(
    ChunkRegistry& world,
    const std::string& dimension_id,
    int32_t block_x, int32_t block_y, int32_t block_z) {
    ResolvedCell out;
    out.chunk_x = floor_div_chunk(block_x);
    out.chunk_y = floor_div_chunk(block_y);
    out.chunk_z = floor_div_chunk(block_z);
    out.local_x = block_x - out.chunk_x * ChunkData::kChunkSize;
    out.local_y = block_y - out.chunk_y * ChunkData::kChunkSize;
    out.local_z = block_z - out.chunk_z * ChunkData::kChunkSize;

    out.chunk = world.get_chunk(
        dimension_id, out.chunk_x, out.chunk_y, out.chunk_z);
    if (!out.chunk) return out;
    if (!out.chunk->terrain.is_valid_cell(
            out.local_x, out.local_y, out.local_z)) {
        out.chunk = nullptr;
        return out;
    }
    out.cell = &out.chunk->terrain.cell_at(
        out.local_x, out.local_y, out.local_z);
    return out;
}

const TerrainCell* resolve_const_cell(
    const ChunkRegistry& world,
    const std::string& dimension_id,
    int32_t block_x, int32_t block_y, int32_t block_z) {
    const int cx = floor_div_chunk(block_x);
    const int cy = floor_div_chunk(block_y);
    const int cz = floor_div_chunk(block_z);
    const int lx = block_x - cx * ChunkData::kChunkSize;
    const int ly = block_y - cy * ChunkData::kChunkSize;
    const int lz = block_z - cz * ChunkData::kChunkSize;

    const ChunkData* chunk = world.get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return nullptr;
    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) return nullptr;
    return &chunk->terrain.cell_at(lx, ly, lz);
}

bool is_air_cell(const TerrainCell& cell, TerrainMaterialId air) {
    return static_cast<TerrainMaterialId>(cell.material) == air && !cell.has_fluid();
}

TerrainMaterialId air_material_for(const WorldGenConfigSnapshot* config) {
    return config ? config->roles.air : 0;
}

void expand_bounds(DynamicStructureSnapshot& snapshot,
                   int32_t lx, int32_t ly, int32_t lz) {
    if (snapshot.blocks.empty()) {
        snapshot.min_x = snapshot.max_x = lx;
        snapshot.min_y = snapshot.max_y = ly;
        snapshot.min_z = snapshot.max_z = lz;
        return;
    }
    if (lx < snapshot.min_x) snapshot.min_x = lx;
    if (ly < snapshot.min_y) snapshot.min_y = ly;
    if (lz < snapshot.min_z) snapshot.min_z = lz;
    if (lx > snapshot.max_x) snapshot.max_x = lx;
    if (ly > snapshot.max_y) snapshot.max_y = ly;
    if (lz > snapshot.max_z) snapshot.max_z = lz;
}

MobileTerrainDelta make_delta(const std::string& dimension_id,
                              const ResolvedCell& cell,
                              TerrainMaterialId old_material,
                              TerrainMaterialId new_material) {
    MobileTerrainDelta delta;
    delta.dimension_id = dimension_id;
    delta.chunk_x = cell.chunk_x;
    delta.chunk_y = cell.chunk_y;
    delta.chunk_z = cell.chunk_z;
    delta.local_x = cell.local_x;
    delta.local_y = cell.local_y;
    delta.local_z = cell.local_z;
    delta.old_material = old_material;
    delta.new_material = new_material;
    return delta;
}

int32_t rounded_to_block(double value) {
    return static_cast<int32_t>(std::llround(value));
}

bool is_identity_rotation(const DynamicStructureTransform& transform) {
    constexpr float kEpsilon = 0.0001f;
    return std::abs(transform.rotation_x) <= kEpsilon
        && std::abs(transform.rotation_y) <= kEpsilon
        && std::abs(transform.rotation_z) <= kEpsilon
        && std::abs(transform.rotation_w - 1.0f) <= kEpsilon;
}

} // namespace

size_t BlockPos3iHash::operator()(const BlockPos3i& p) const {
    size_t h = std::hash<int32_t>()(p.x);
    h ^= std::hash<int32_t>()(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

DynamicStructureId DynamicStructureRegistry::add(DynamicStructureEntity entity) {
    const DynamicStructureId id = next_id_++;
    entity.id = id;
    structures_[id] = std::move(entity);
    return id;
}

DynamicStructureEntity* DynamicStructureRegistry::get(DynamicStructureId id) {
    auto it = structures_.find(id);
    return it != structures_.end() ? &it->second : nullptr;
}

const DynamicStructureEntity* DynamicStructureRegistry::get(DynamicStructureId id) const {
    auto it = structures_.find(id);
    return it != structures_.end() ? &it->second : nullptr;
}

bool DynamicStructureRegistry::remove(DynamicStructureId id) {
    return structures_.erase(id) > 0;
}

void DynamicStructureRegistry::clear() {
    structures_.clear();
    next_id_ = 1;
}

AssembleResult DynamicStructureAssembler::assemble_connected(
    ChunkRegistry& world,
    DynamicStructureRegistry& registry,
    const WorldGenConfigSnapshot* worldgen_config,
    const std::string& dimension_id,
    int32_t seed_x, int32_t seed_y, int32_t seed_z,
    IncludePredicate include,
    const AssembleOptions& options) {
    AssembleResult result;

    if (!include) {
        result.error = "include predicate is empty";
        return result;
    }
    if (options.max_blocks == 0) {
        result.error = "max_blocks must be greater than zero";
        return result;
    }

    const TerrainCell* seed_cell = resolve_const_cell(
        world, dimension_id, seed_x, seed_y, seed_z);
    if (!seed_cell) {
        result.error = "seed cell is not loaded";
        return result;
    }
    if (!include(*seed_cell)) {
        result.error = "seed cell is not assemblable";
        return result;
    }

    std::queue<BlockPos3i> open;
    std::unordered_set<BlockPos3i, BlockPos3iHash> visited;
    std::vector<BlockPos3i> world_blocks;
    DynamicStructureSnapshot snapshot;

    open.push(BlockPos3i{seed_x, seed_y, seed_z});
    visited.insert(BlockPos3i{seed_x, seed_y, seed_z});

    static constexpr int kOffsets[6][3] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1},
    };

    while (!open.empty()) {
        const BlockPos3i pos = open.front();
        open.pop();

        const TerrainCell* cell = resolve_const_cell(
            world, dimension_id, pos.x, pos.y, pos.z);
        if (!cell) {
            if (options.fail_on_missing_chunks) {
                result.error = "assembly reached an unloaded source cell";
                return result;
            }
            continue;
        }
        if (!include(*cell)) continue;

        if (world_blocks.size() >= options.max_blocks) {
            result.hit_block_limit = true;
            result.error = "assembly exceeded max_blocks";
            return result;
        }

        world_blocks.push_back(pos);
        const int32_t lx = pos.x - seed_x;
        const int32_t ly = pos.y - seed_y;
        const int32_t lz = pos.z - seed_z;
        expand_bounds(snapshot, lx, ly, lz);
        snapshot.blocks.push_back(LocalStructureBlock{
            lx, ly, lz,
            static_cast<TerrainMaterialId>(cell->material),
            cell->flags,
        });

        for (const auto& offset : kOffsets) {
            BlockPos3i next{
                pos.x + offset[0],
                pos.y + offset[1],
                pos.z + offset[2],
            };
            if (visited.find(next) != visited.end()) continue;

            const TerrainCell* next_cell = resolve_const_cell(
                world, dimension_id, next.x, next.y, next.z);
            if (!next_cell) {
                if (options.fail_on_missing_chunks) {
                    result.error = "assembly reached an unloaded neighbor chunk";
                    return result;
                }
                visited.insert(next);
                continue;
            }
            if (!include(*next_cell)) {
                visited.insert(next);
                continue;
            }

            visited.insert(next);
            open.push(next);
        }
    }

    if (snapshot.blocks.empty()) {
        result.error = "no blocks were assembled";
        return result;
    }

    const TerrainMaterialId air = air_material_for(worldgen_config);
    if (options.clear_source_cells) {
        result.terrain_deltas.reserve(world_blocks.size());
        for (const BlockPos3i& pos : world_blocks) {
            ResolvedCell cell = resolve_cell(world, dimension_id, pos.x, pos.y, pos.z);
            if (!cell.cell) {
                result.error = "source cell disappeared during assembly";
                return result;
            }
            const TerrainMaterialId old_material =
                static_cast<TerrainMaterialId>(cell.cell->material);
            cell.chunk->terrain.set_cell(cell.local_x, cell.local_y, cell.local_z, air, 0);
            cell.chunk->terrain.cell_at(cell.local_x, cell.local_y, cell.local_z).clear_fluid();
            result.terrain_deltas.push_back(
                make_delta(dimension_id, cell, old_material, air));
        }
    }

    DynamicStructureEntity entity;
    entity.transform.dimension_id = dimension_id;
    entity.transform.position_x = static_cast<double>(seed_x);
    entity.transform.position_y = static_cast<double>(seed_y);
    entity.transform.position_z = static_cast<double>(seed_z);
    entity.snapshot = std::move(snapshot);
    entity.mass = static_cast<double>(entity.snapshot.blocks.size());
    entity.moving = options.clear_source_cells;
    entity.dirty_mesh = true;

    const DynamicStructureId id = registry.add(std::move(entity));
    result.success = true;
    result.structure_id = id;
    const DynamicStructureEntity* stored = registry.get(id);
    result.block_count = stored ? stored->snapshot.block_count() : 0;
    return result;
}

DisassembleResult DynamicStructureAssembler::disassemble_to_world(
    ChunkRegistry& world,
    DynamicStructureRegistry& registry,
    const WorldGenConfigSnapshot* worldgen_config,
    DynamicStructureId structure_id,
    const DisassembleOptions& options) {
    DisassembleResult result;

    DynamicStructureEntity* entity = registry.get(structure_id);
    if (!entity) {
        result.error = "dynamic structure not found";
        return result;
    }
    if (entity->snapshot.empty()) {
        result.error = "dynamic structure snapshot is empty";
        return result;
    }
    if (!is_identity_rotation(entity->transform)) {
        result.error = "rotated disassembly is not implemented yet";
        return result;
    }

    // First validate all destinations. First version supports translational
    // write-back only. Rotation is retained for rendering/sync, but docking a
    // rotated ship needs a later voxelization/resampling pass.
    const int32_t base_x = rounded_to_block(entity->transform.position_x);
    const int32_t base_y = rounded_to_block(entity->transform.position_y);
    const int32_t base_z = rounded_to_block(entity->transform.position_z);
    const std::string& dimension_id = entity->transform.dimension_id;
    const TerrainMaterialId air = air_material_for(worldgen_config);

    for (const LocalStructureBlock& block : entity->snapshot.blocks) {
        const int32_t wx = base_x + block.local_x;
        const int32_t wy = base_y + block.local_y;
        const int32_t wz = base_z + block.local_z;
        const TerrainCell* dst = resolve_const_cell(world, dimension_id, wx, wy, wz);
        if (!dst) {
            result.error = "disassembly destination chunk is not loaded";
            return result;
        }
        if (options.require_air_destination && !is_air_cell(*dst, air)) {
            result.error = "disassembly destination is occupied";
            return result;
        }
    }

    result.terrain_deltas.reserve(entity->snapshot.blocks.size());
    for (const LocalStructureBlock& block : entity->snapshot.blocks) {
        const int32_t wx = base_x + block.local_x;
        const int32_t wy = base_y + block.local_y;
        const int32_t wz = base_z + block.local_z;
        ResolvedCell dst = resolve_cell(world, dimension_id, wx, wy, wz);
        if (!dst.cell) {
            result.error = "disassembly destination disappeared";
            return result;
        }

        const TerrainMaterialId old_material =
            static_cast<TerrainMaterialId>(dst.cell->material);
        dst.chunk->terrain.set_cell(
            dst.local_x, dst.local_y, dst.local_z,
            block.material, block.flags);
        dst.chunk->terrain.cell_at(dst.local_x, dst.local_y, dst.local_z).clear_fluid();
        result.terrain_deltas.push_back(
            make_delta(dimension_id, dst, old_material, block.material));
    }

    result.success = true;
    result.block_count = entity->snapshot.block_count();

    if (options.remove_entity) {
        registry.remove(structure_id);
    } else {
        entity->moving = false;
        entity->dirty_mesh = true;
    }

    return result;
}

DynamicStructureTransformSnapshot DynamicStructureAssembler::make_transform_snapshot(
    const DynamicStructureEntity& entity,
    int64_t tick) {
    DynamicStructureTransformSnapshot snapshot;
    snapshot.structure_id = entity.id;
    snapshot.structure_version = entity.structure_version;
    snapshot.tick = tick;
    snapshot.transform = entity.transform;
    return snapshot;
}

} // namespace snt::data::mobile_structure
