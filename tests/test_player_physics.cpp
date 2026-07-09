#include "data/defs/terrain_data.h"
#include "data/world/chunk_registry.h"
#include "player/ray_cast.h"
#include "player/voxel_collision.h"

#include <gtest/gtest.h>

#include <utility>

namespace {

snt::data::ChunkData make_empty_chunk() {
    snt::data::ChunkData chunk;
    chunk.terrain.resize(
        snt::data::ChunkData::kChunkSize,
        snt::data::ChunkData::kChunkSize,
        snt::data::ChunkData::kChunkSize);
    return chunk;
}

}  // namespace

TEST(PlayerVoxelCollisionTest, DownwardMoveStopsOnSolidVoxelTop) {
    snt::data::ChunkRegistry registry;
    auto chunk = make_empty_chunk();
    for (int z = 0; z < chunk.terrain.size_z; ++z) {
        for (int x = 0; x < chunk.terrain.size_x; ++x) {
            chunk.terrain.set_cell(x, 0, z, 1, snt::data::TF_SOLID);
        }
    }
    registry.set_chunk("overworld", 0, 0, 0, std::move(chunk));

    const snt::player::CollisionWorldView world{
        .chunks = &registry,
        .dimension_id = "overworld",
        .missing_chunks_are_solid = false,
    };
    const snt::player::Aabb body{
        .min = {1.25f, 1.20f, 1.25f},
        .max = {1.75f, 3.00f, 1.75f},
    };

    const snt::player::CollisionMoveResult move =
        snt::player::move_aabb_collide_voxels(world, body, {0.0f, -1.0f, 0.0f});

    EXPECT_TRUE(move.hit_y);
    EXPECT_TRUE(move.grounded);
    EXPECT_FLOAT_EQ(move.delta.y, -0.20f);
}

TEST(PlayerRayCastTest, DdaHitsExpectedVoxelAndFaceNormal) {
    snt::data::ChunkRegistry registry;
    auto chunk = make_empty_chunk();
    chunk.terrain.set_cell(3, 4, 5, 1, snt::data::TF_SOLID);
    registry.set_chunk("overworld", 0, 0, 0, std::move(chunk));

    const snt::player::CollisionWorldView world{
        .chunks = &registry,
        .dimension_id = "overworld",
        .missing_chunks_are_solid = false,
    };

    const snt::player::RayCastResult hit =
        snt::player::ray_cast_voxels_dda(
            world,
            {3.5f, 4.5f, 1.5f},
            {0.0f, 0.0f, 1.0f},
            8.0f);

    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.block.x, 3);
    EXPECT_EQ(hit.block.y, 4);
    EXPECT_EQ(hit.block.z, 5);
    EXPECT_EQ(hit.previous.z, 4);
    EXPECT_EQ(hit.normal.x, 0);
    EXPECT_EQ(hit.normal.y, 0);
    EXPECT_EQ(hit.normal.z, -1);
}

TEST(PlayerRayCastTest, DdaHandlesVoxelEdgeTie) {
    snt::data::ChunkRegistry registry;
    auto chunk = make_empty_chunk();
    chunk.terrain.set_cell(2, 4, 2, 1, snt::data::TF_SOLID);
    registry.set_chunk("overworld", 0, 0, 0, std::move(chunk));

    const snt::player::CollisionWorldView world{
        .chunks = &registry,
        .dimension_id = "overworld",
        .missing_chunks_are_solid = false,
    };

    const snt::player::RayCastResult hit =
        snt::player::ray_cast_voxels_dda(
            world,
            {1.5f, 4.5f, 1.5f},
            {1.0f, 0.0f, 1.0f},
            4.0f);

    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.block.x, 2);
    EXPECT_EQ(hit.block.y, 4);
    EXPECT_EQ(hit.block.z, 2);
    EXPECT_EQ(hit.normal.x, -1);
    EXPECT_EQ(hit.normal.y, 0);
    EXPECT_EQ(hit.normal.z, -1);
}
