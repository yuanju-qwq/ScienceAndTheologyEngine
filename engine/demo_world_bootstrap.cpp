#define SNT_LOG_CHANNEL "engine"
#include "core/log.h"

#include "engine/demo_world_bootstrap.h"

#include "data/defs/chunk_data.h"
#include "data/defs/world_seed.h"
#include "data/world/chunk_registry.h"
#include "data/world_gen/terrain_generator.h"
#include "data/world_gen/world_gen_config.h"
#include "voxel/chunk_render_system.h"

#include <memory>

namespace snt::engine {
namespace {

std::shared_ptr<const snt::data::WorldGenConfigSnapshot> make_demo_world_gen_config() {
    using namespace snt::data;

    auto config = std::make_shared<WorldGenConfigSnapshot>();

    TerrainMaterialDef air;
    air.id = 0;
    air.key = "air";
    config->materials.push_back(air);
    config->material_ids_by_key[air.key] = air.id;
    config->material_keys_by_id[air.id] = air.key;

    TerrainMaterialDef stone;
    stone.id = 1;
    stone.key = "stone";
    stone.flags = TF_SOLID | TF_MINEABLE | TF_WALKABLE;
    config->materials.push_back(stone);
    config->material_ids_by_key[stone.key] = stone.id;
    config->material_keys_by_id[stone.id] = stone.key;

    config->roles.air = air.id;
    config->roles.stone = stone.id;
    config->roles.dirt = stone.id;

    BaseTerrainRule rule;
    rule.dimension_id = "overworld";
    rule.default_material = stone.id;
    rule.high_elevation_material = stone.id;
    rule.cave_threshold = 10.0f;
    config->base_terrain_rules.push_back(rule);

    config->content_hash = hash_world_gen_config(*config);
    return config;
}

size_t count_non_air_cells(const snt::data::ChunkData& chunk,
                           snt::data::TerrainMaterialId air_material) {
    size_t non_air = 0;
    for (const auto& cell : chunk.terrain.cells) {
        if (static_cast<snt::data::TerrainMaterialId>(cell.material) != air_material) {
            ++non_air;
        }
    }
    return non_air;
}

}  // namespace

snt::core::Expected<void> bootstrap_demo_world(
        const DemoWorldBootstrapDesc& desc,
        snt::data::ChunkRegistry& chunk_registry,
        snt::voxel::ChunkRenderSystem& chunk_render_system) {
    if (!desc.enabled) {
        SNT_LOG_INFO("Demo chunk bootstrap disabled");
        return {};
    }

    auto demo_world_gen = make_demo_world_gen_config();
    snt::data::TerrainGenerator terrain_gen(
        snt::data::WorldSeed(desc.seed),
        demo_world_gen);

    constexpr int32_t kDemoChunks[][3] = {
        {0,  0, 0},
        {0, -1, 0},
    };

    for (auto [cx, cy, cz] : kDemoChunks) {
        snt::data::ChunkData c = terrain_gen.generate_chunk("overworld", cx, cy, cz);
        const size_t non_air = count_non_air_cells(c, demo_world_gen->roles.air);
        const size_t total = c.terrain.cells.size();
        chunk_registry.set_chunk("overworld", cx, cy, cz, std::move(c));
        chunk_render_system.mark_dirty(snt::data::ChunkKey("overworld", cx, cy, cz));
        SNT_LOG_INFO("Demo chunk generated at (%d,%d,%d), non_air=%zu/%zu",
                     cx, cy, cz, non_air, total);
    }

    return {};
}

}  // namespace snt::engine
