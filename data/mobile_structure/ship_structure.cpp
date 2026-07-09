// ShipStructureService implementation.
//
// Ported from src/core/mobile_structure/ship_structure.cpp.
// P2 task 4 refactor: WorldData god-object replaced with explicit
// ChunkRegistry + DynamicStructureRegistry + tick parameters.
// Namespace: science_and_theology::mobile_structure -> snt::data.

#include "ship_structure.h"

#include <algorithm>

#include "../world/chunk_registry.h"

namespace snt::data {

bool ShipStructureService::material_allowed(
    TerrainMaterialId material,
    const std::vector<TerrainMaterialId>& allowed_materials) {
    if (allowed_materials.empty()) {
        return material != 0;
    }
    return std::find(allowed_materials.begin(), allowed_materials.end(), material)
        != allowed_materials.end();
}

ShipAssembleResult ShipStructureService::assemble_ship_from_world(
    ChunkRegistry& chunks,
    DynamicStructureRegistry& registry,
    const WorldGenConfigSnapshot* worldgen_config,
    int64_t current_tick,
    const std::string& dimension_id,
    int32_t seed_x, int32_t seed_y, int32_t seed_z,
    const ShipAssembleOptions& options) {
    AssembleOptions assemble_options;
    assemble_options.max_blocks = options.max_blocks;
    assemble_options.clear_source_cells = options.clear_source_cells;
    assemble_options.fail_on_missing_chunks = options.fail_on_missing_chunks;

    auto include = [&options](const TerrainCell& cell) {
        return material_allowed(
            static_cast<TerrainMaterialId>(cell.material),
            options.allowed_materials);
    };

    ShipAssembleResult result;
    result.base = DynamicStructureAssembler::assemble_connected(
        chunks,
        registry,
        worldgen_config,
        dimension_id,
        seed_x, seed_y, seed_z,
        include,
        assemble_options);

    if (result.base.success) {
        const DynamicStructureEntity* entity =
            registry.get(result.base.structure_id);
        if (entity != nullptr) {
            result.transform_snapshot =
                DynamicStructureAssembler::make_transform_snapshot(
                    *entity, current_tick);
        }
    }

    return result;
}

DisassembleResult ShipStructureService::disassemble_ship_to_world(
    ChunkRegistry& chunks,
    DynamicStructureRegistry& registry,
    const WorldGenConfigSnapshot* worldgen_config,
    DynamicStructureId ship_id,
    const DisassembleOptions& options) {
    return DynamicStructureAssembler::disassemble_to_world(
        chunks,
        registry,
        worldgen_config,
        ship_id,
        options);
}

DynamicStructureTransformSnapshot ShipStructureService::make_transform_snapshot(
    const DynamicStructureRegistry& registry,
    DynamicStructureId ship_id,
    int64_t tick) {
    DynamicStructureTransformSnapshot snapshot;
    const DynamicStructureEntity* entity =
        registry.get(ship_id);
    if (entity == nullptr) return snapshot;
    return DynamicStructureAssembler::make_transform_snapshot(*entity, tick);
}

} // namespace snt::data
