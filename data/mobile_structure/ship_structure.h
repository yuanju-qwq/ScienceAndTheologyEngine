#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dynamic_structure.h"

namespace snt::data {

class ChunkRegistry;

struct ShipAssembleOptions {
    size_t max_blocks = 4096;
    bool clear_source_cells = true;
    bool fail_on_missing_chunks = false;
    std::vector<TerrainMaterialId> allowed_materials;
};

struct ShipAssembleResult {
    AssembleResult base;
    DynamicStructureTransformSnapshot transform_snapshot;
};

// Thin semantic wrapper over DynamicStructureAssembler for ship/station cores.
// It keeps gameplay code from depending directly on low-level flood-fill
// predicates and gives future ship rules a clear insertion point.
//
// P2 task 4 refactor: methods now take ChunkRegistry + DynamicStructureRegistry
// + tick explicitly instead of the legacy WorldData god-object. Callers
// (ECS systems) obtain these from the world context entity.
class ShipStructureService {
public:
    static bool material_allowed(
        TerrainMaterialId material,
        const std::vector<TerrainMaterialId>& allowed_materials);

    static ShipAssembleResult assemble_ship_from_world(
        ChunkRegistry& chunks,
        DynamicStructureRegistry& registry,
        const WorldGenConfigSnapshot* worldgen_config,
        int64_t current_tick,
        const std::string& dimension_id,
        int32_t seed_x, int32_t seed_y, int32_t seed_z,
        const ShipAssembleOptions& options = ShipAssembleOptions{});

    static DisassembleResult disassemble_ship_to_world(
        ChunkRegistry& chunks,
        DynamicStructureRegistry& registry,
        const WorldGenConfigSnapshot* worldgen_config,
        DynamicStructureId ship_id,
        const DisassembleOptions& options = DisassembleOptions{});

    static DynamicStructureTransformSnapshot make_transform_snapshot(
        const DynamicStructureRegistry& registry,
        DynamicStructureId ship_id,
        int64_t tick);
};


} // namespace snt::data
