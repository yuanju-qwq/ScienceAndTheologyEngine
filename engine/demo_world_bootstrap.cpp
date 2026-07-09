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

    TerrainMaterialDef dirt;
    dirt.id = 2;
    dirt.key = "dirt";
    dirt.flags = TF_SOLID | TF_MINEABLE | TF_WALKABLE;
    config->materials.push_back(dirt);
    config->material_ids_by_key[dirt.key] = dirt.id;
    config->material_keys_by_id[dirt.id] = dirt.key;

    TerrainMaterialDef sand;
    sand.id = 3;
    sand.key = "sand";
    sand.flags = TF_SOLID | TF_MINEABLE | TF_WALKABLE | TF_GRAVITY_FALL;
    config->materials.push_back(sand);
    config->material_ids_by_key[sand.key] = sand.id;
    config->material_keys_by_id[sand.id] = sand.key;

    TerrainMaterialDef snow;
    snow.id = 4;
    snow.key = "snow";
    snow.flags = TF_SOLID | TF_MINEABLE | TF_WALKABLE;
    config->materials.push_back(snow);
    config->material_ids_by_key[snow.key] = snow.id;
    config->material_keys_by_id[snow.id] = snow.key;

    config->roles.air = air.id;
    config->roles.stone = stone.id;
    config->roles.dirt = dirt.id;

    BaseTerrainRule rule;
    rule.dimension_id = "overworld";
    rule.default_material = stone.id;
    rule.high_elevation_material = stone.id;
    rule.cave_threshold = 10.0f;
    config->base_terrain_rules.push_back(rule);

    config->content_hash = hash_world_gen_config(*config);
    return config;
}

void paint_demo_surface_variants(
        snt::data::ChunkData& chunk,
        const snt::data::WorldGenConfigSnapshot& config) {
    for (int z = 0; z < chunk.terrain.size_z; ++z) {
        for (int x = 0; x < chunk.terrain.size_x; ++x) {
            for (int y = chunk.terrain.size_y - 1; y >= 0; --y) {
                auto& cell = chunk.terrain.cell_at(x, y, z);
                if (static_cast<snt::data::TerrainMaterialId>(cell.material) == config.roles.air) {
                    continue;
                }

                const int world_x = chunk.chunk_x * snt::data::ChunkData::kChunkSize + x;
                const int world_z = chunk.chunk_z * snt::data::ChunkData::kChunkSize + z;
                const int band = ((world_x / 8) + (world_z / 8)) & 3;
                if (band == 1) {
                    cell.material = config.material_ids_by_key.at("dirt");
                } else if (band == 2) {
                    cell.material = config.material_ids_by_key.at("sand");
                } else if (band == 3) {
                    cell.material = config.material_ids_by_key.at("snow");
                }
                break;
            }
        }
    }
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
        paint_demo_surface_variants(c, *demo_world_gen);
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
