// Unit tests for the data layer (P2 tasks 1-4).
//
// Validates:
//   - ChunkRegistry chunk storage and lookup.
//   - ChunkSerializer save/load round-trip.
//   - RegionFile write/read round-trip.
//   - BlockEntityRegistry registration and spatial query.
//   - MachineCollisionOverlay mark/query.
//   - TerrainGenerator produces a valid chunk (basic smoke test).
//   - NoiseGenerator is deterministic for the same seed.

#include "data/defs/chunk_data.h"
#include "data/defs/block_entity_registry.h"
#include "data/defs/machine_collision_overlay.h"
#include "data/save/region_file.h"
#include "data/save/chunk_serializer.h"
#include "data/world/chunk_registry.h"
#include "data/world_gen/noise_generator.h"
#include "data/world_gen/terrain_generator.h"
#include "data/world_gen/world_gen_config.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace snt::data;

// ===========================================================================
// ChunkRegistry
// ===========================================================================

TEST(ChunkRegistryTest, SetAndGetChunk) {
    ChunkRegistry registry;
    ChunkData chunk;
    chunk.terrain.resize(8, 8, 8);
    registry.set_chunk("overworld", 1, 2, 3, std::move(chunk));

    EXPECT_TRUE(registry.has_chunk("overworld", 1, 2, 3));
    ASSERT_NE(registry.get_chunk("overworld", 1, 2, 3), nullptr);
    EXPECT_EQ(registry.chunk_count(), 1u);
}

TEST(ChunkRegistryTest, GetMissingChunkReturnsNull) {
    ChunkRegistry registry;
    EXPECT_FALSE(registry.has_chunk("overworld", 0, 0, 0));
    EXPECT_EQ(registry.get_chunk("overworld", 0, 0, 0), nullptr);
}

TEST(ChunkRegistryTest, RemoveChunk) {
    ChunkRegistry registry;
    ChunkData chunk;
    registry.set_chunk("overworld", 0, 0, 0, std::move(chunk));
    EXPECT_EQ(registry.chunk_count(), 1u);

    registry.remove_chunk("overworld", 0, 0, 0);
    EXPECT_FALSE(registry.has_chunk("overworld", 0, 0, 0));
    EXPECT_EQ(registry.chunk_count(), 0u);
}

TEST(ChunkRegistryTest, AllChunkKeys) {
    ChunkRegistry registry;
    ChunkData a, b;
    registry.set_chunk("overworld", 1, 0, 0, std::move(a));
    registry.set_chunk("nether", 0, 1, 0, std::move(b));

    auto keys = registry.all_chunk_keys();
    ASSERT_EQ(keys.size(), 2u);
}

TEST(ChunkRegistryTest, DifferentDimensionsAreIndependent) {
    ChunkRegistry registry;
    ChunkData a, b;
    registry.set_chunk("overworld", 0, 0, 0, std::move(a));
    registry.set_chunk("nether", 0, 0, 0, std::move(b));

    EXPECT_NE(registry.get_chunk("overworld", 0, 0, 0), nullptr);
    EXPECT_NE(registry.get_chunk("nether", 0, 0, 0), nullptr);
    EXPECT_EQ(registry.chunk_count(), 2u);
}

// ===========================================================================
// ChunkSerializer round-trip
// ===========================================================================

TEST(ChunkSerializerTest, RoundTripBasicChunk) {
    ChunkData original;
    original.chunk_x = 5;
    original.chunk_y = -3;
    original.chunk_z = 7;
    original.state = ChunkState::ACTIVE;
    original.terrain.resize(4, 4, 4);
    original.terrain.cell_at(0, 0, 0).material = 1;
    original.terrain.cell_at(1, 2, 3).material = 2;

    std::vector<uint8_t> blob = ChunkSerializer::serialize("overworld", original);
    ASSERT_FALSE(blob.empty());

    ChunkData restored;
    std::string dim_id;
    ASSERT_TRUE(ChunkSerializer::deserialize(blob, dim_id, restored));

    EXPECT_EQ(dim_id, "overworld");
    EXPECT_EQ(restored.chunk_x, original.chunk_x);
    EXPECT_EQ(restored.chunk_y, original.chunk_y);
    EXPECT_EQ(restored.chunk_z, original.chunk_z);
    EXPECT_EQ(restored.state, original.state);
}

// ===========================================================================
// RegionFile round-trip
// ===========================================================================

TEST(RegionFileTest, WriteAndReadRoundTrip) {
    std::string tmp_dir = std::filesystem::temp_directory_path().string();
    std::string file_path = tmp_dir + "/snt_test_region.region";

    std::vector<RegionChunkEntry> chunks;
    RegionChunkEntry e1;
    e1.local_x = 0;
    e1.local_y = 0;
    e1.local_z = 0;
    e1.data = {1, 2, 3, 4, 5};
    chunks.push_back(e1);

    RegionChunkEntry e2;
    e2.local_x = 1;
    e2.local_y = 2;
    e2.local_z = 3;
    e2.data = {9, 8, 7};
    chunks.push_back(e2);

    ASSERT_TRUE(RegionFile::write(file_path, 0, 0, 0, "overworld", chunks));

    std::string out_dim;
    int rx, ry, rz;
    std::vector<RegionChunkEntry> out_chunks;
    ASSERT_TRUE(RegionFile::read(file_path, out_dim, rx, ry, rz, out_chunks));

    EXPECT_EQ(out_dim, "overworld");
    EXPECT_EQ(rx, 0);
    EXPECT_EQ(ry, 0);
    EXPECT_EQ(rz, 0);
    ASSERT_EQ(out_chunks.size(), 2u);

    std::filesystem::remove(file_path);
}

TEST(RegionFileTest, ToRegionAndToLocal) {
    EXPECT_EQ(RegionFile::to_region(0), 0);
    EXPECT_EQ(RegionFile::to_region(31), 0);
    EXPECT_EQ(RegionFile::to_region(32), 1);
    EXPECT_EQ(RegionFile::to_region(-1), -1);
    EXPECT_EQ(RegionFile::to_region(-32), -1);

    EXPECT_EQ(RegionFile::to_local(0), 0);
    EXPECT_EQ(RegionFile::to_local(31), 31);
    EXPECT_EQ(RegionFile::to_local(32), 0);
    EXPECT_EQ(RegionFile::to_local(-1), 31);
}

// ===========================================================================
// BlockEntityRegistry
// ===========================================================================

TEST(BlockEntityRegistryTest, RegisterTreeAndQuery) {
    BlockEntityRegistry registry;

    std::vector<OwnedCell> cells;
    cells.push_back(OwnedCell{0, 0, 0});
    cells.push_back(OwnedCell{0, 1, 0});

    EntityId id = registry.register_tree_entity(
        "overworld", 10, 20, 30, "oak", TreeGrowthStage::MATURE, 1000, cells);

    EXPECT_NE(id.id, 0u);
    EXPECT_EQ(registry.size(), 1u);
    EXPECT_EQ(registry.get_entity_type(id), BlockEntityType::TREE);

    const TreeBlockEntityState* state = registry.get_tree_state(id);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->species_key, "oak");
    EXPECT_EQ(state->growth_stage, TreeGrowthStage::MATURE);
}

TEST(BlockEntityRegistryTest, FindOwnerAt) {
    BlockEntityRegistry registry;
    std::vector<OwnedCell> cells;
    cells.push_back(OwnedCell{5, 10, 15});

    EntityId id = registry.register_tree_entity(
        "overworld", 5, 10, 15, "oak", TreeGrowthStage::YOUNG, 0, cells);

    EXPECT_EQ(registry.find_owner_at(5, 10, 15), id);
    EXPECT_EQ(registry.find_owner_at(6, 10, 15), EntityId{});
}

TEST(BlockEntityRegistryTest, RemoveEntity) {
    BlockEntityRegistry registry;
    std::vector<OwnedCell> cells;
    cells.push_back(OwnedCell{1, 1, 1});

    EntityId id = registry.register_tree_entity(
        "overworld", 1, 1, 1, "oak", TreeGrowthStage::YOUNG, 0, cells);
    EXPECT_EQ(registry.size(), 1u);

    registry.remove_entity(id);
    EXPECT_EQ(registry.size(), 0u);
    EXPECT_EQ(registry.find_owner_at(1, 1, 1), EntityId{});
}

// ===========================================================================
// MachineCollisionOverlay
// ===========================================================================

TEST(MachineCollisionOverlayTest, MarkAndQuery) {
    MachineCollisionOverlay overlay;
    EXPECT_FALSE(overlay.is_occupied("overworld", 10, 20, 30));

    overlay.mark("overworld", 10, 20, 30);
    EXPECT_TRUE(overlay.is_occupied("overworld", 10, 20, 30));
    EXPECT_EQ(overlay.size(), 1u);

    overlay.clear("overworld", 10, 20, 30);
    EXPECT_FALSE(overlay.is_occupied("overworld", 10, 20, 30));
    EXPECT_EQ(overlay.size(), 0u);
}

TEST(MachineCollisionOverlayTest, ClearDimension) {
    MachineCollisionOverlay overlay;
    overlay.mark("overworld", 1, 0, 0);
    overlay.mark("overworld", 2, 0, 0);
    overlay.mark("nether", 1, 0, 0);

    EXPECT_EQ(overlay.size(), 3u);
    EXPECT_EQ(overlay.clear_dimension("overworld"), 2u);
    EXPECT_EQ(overlay.size(), 1u);
    EXPECT_TRUE(overlay.is_occupied("nether", 1, 0, 0));
}

// ===========================================================================
// NoiseGenerator
// ===========================================================================

TEST(NoiseGeneratorTest, DeterministicForSameSeed) {
    NoiseGenerator a(42);
    NoiseGenerator b(42);
    NoiseGenerator c(99);

    float va = a.noise_3d(1.5f, 2.5f, 3.5f, 4, 0.5f);
    float vb = b.noise_3d(1.5f, 2.5f, 3.5f, 4, 0.5f);
    float vc = c.noise_3d(1.5f, 2.5f, 3.5f, 4, 0.5f);

    EXPECT_FLOAT_EQ(va, vb);
    // Different seed should (very likely) produce different output.
    EXPECT_NE(va, vc);
}

TEST(NoiseGeneratorTest, OutputRange) {
    NoiseGenerator noise(7);
    // Sample many points and check the output stays roughly in [-1, 1].
    for (int i = 0; i < 100; ++i) {
        float v = noise.noise_3d(
            static_cast<float>(i) * 0.1f,
            static_cast<float>(i) * 0.2f,
            static_cast<float>(i) * 0.3f);
        EXPECT_GE(v, -1.5f);
        EXPECT_LE(v, 1.5f);
    }
}

// ===========================================================================
// TerrainGenerator (smoke test)
// ===========================================================================

TEST(TerrainGeneratorTest, GeneratesValidChunkSize) {
    // Build a minimal world-gen config with air + stone roles.
    auto config = std::make_shared<WorldGenConfigSnapshot>();
    TerrainMaterialDef air_def;
    air_def.id = 0;
    air_def.key = "air";
    config->materials.push_back(air_def);
    config->material_ids_by_key["air"] = 0;
    config->material_keys_by_id[0] = "air";
    config->roles.air = 0;
    config->roles.stone = 0;

    TerrainMaterialDef stone_def;
    stone_def.id = 1;
    stone_def.key = "stone";
    config->materials.push_back(stone_def);
    config->material_ids_by_key["stone"] = 1;
    config->material_keys_by_id[1] = "stone";
    config->roles.stone = 1;

    BaseTerrainRule rule;
    rule.dimension_id = "overworld";
    rule.default_material = 1;
    config->base_terrain_rules.push_back(rule);

    config->content_hash = hash_world_gen_config(*config);

    WorldSeed seed(12345);

    TerrainGenerator generator(seed, config);

    ChunkData chunk = generator.generate_chunk("overworld", 0, 0, 0);

    // Verify the terrain was populated (not all zeros).
    bool has_non_air = false;
    for (int i = 0; i < chunk.terrain.size_x && !has_non_air; ++i) {
        for (int j = 0; j < chunk.terrain.size_y && !has_non_air; ++j) {
            for (int k = 0; k < chunk.terrain.size_z && !has_non_air; ++k) {
                if (chunk.terrain.cell_at(i, j, k).material != 0) {
                    has_non_air = true;
                }
            }
        }
    }
    EXPECT_TRUE(has_non_air);
}
