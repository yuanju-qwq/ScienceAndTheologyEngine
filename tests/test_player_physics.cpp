#include "core/job_system.h"
#include "data/defs/terrain_data.h"
#include "data/world/chunk_registry.h"
#include "ecs/components.h"
#include "ecs/system_scheduler.h"
#include "ecs/world.h"
#include "input/input_system.h"
#include "player/collision_snapshot.h"
#include "player/player_controller.h"
#include "player/player_physics_system.h"
#include "player/ray_cast.h"
#include "player/voxel_collision.h"

#include <gtest/gtest.h>

#include <memory>
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

    const snt::player::CollisionWorldView world(&registry, "overworld", false);
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

TEST(PlayerCollisionSnapshotTest, RetainsSweptTerrainAfterRegistryChanges) {
    snt::data::ChunkRegistry registry;
    auto chunk = make_empty_chunk();
    for (int z = 0; z < chunk.terrain.size_z; ++z) {
        for (int x = 0; x < chunk.terrain.size_x; ++x) {
            chunk.terrain.set_cell(x, 0, z, 1, snt::data::TF_SOLID);
        }
    }
    registry.set_chunk("overworld", 0, 0, 0, std::move(chunk));

    const snt::player::CollisionWorldView world(&registry, "overworld", false);
    const snt::player::Aabb body{
        .min = {1.25f, 1.20f, 1.25f},
        .max = {1.75f, 3.00f, 1.75f},
    };
    const auto snapshot = snt::player::VoxelCollisionSnapshot::capture(
        world, body, {0.0f, -1.0f, 0.0f});

    // A worker receives the value snapshot, never the main-thread registry.
    registry.clear();
    const auto move = snt::player::move_aabb_collide_voxels(
        snapshot, body, {0.0f, -1.0f, 0.0f});

    EXPECT_TRUE(move.hit_y);
    EXPECT_TRUE(move.grounded);
    EXPECT_FLOAT_EQ(move.delta.y, -0.20f);
}

TEST(PlayerPhysicsSystemTest, AppliesStateAndCameraAtWorkerBarrier) {
    snt::core::JobSystemP2 jobs;
    jobs.init(2);
    {
        snt::ecs::SystemScheduler scheduler(jobs);
        snt::ecs::World world;
        const entt::entity player_entity = world.create_entity();
        world.registry().emplace<snt::ecs::Transform>(player_entity);

        snt::data::ChunkRegistry chunks;
        auto chunk = make_empty_chunk();
        for (int z = 0; z < chunk.terrain.size_z; ++z) {
            for (int x = 0; x < chunk.terrain.size_x; ++x) {
                chunk.terrain.set_cell(x, 0, z, 1, snt::data::TF_SOLID);
            }
        }
        chunks.set_chunk("overworld", 0, 0, 0, std::move(chunk));

        snt::input::InputSystem input;
        auto controller = std::make_shared<snt::player::PlayerControllerSystem>();
        controller->set_input(&input);
        controller->set_chunk_registry(&chunks);
        controller->set_camera_entity(player_entity);
        controller->set_dimension_id("overworld");
        controller->set_spawn_feet_position({1.5f, 1.2f, 1.5f});
        controller->set_initial_look(-90.0f, -25.0f);
        auto physics = controller->make_physics_system();

        ASSERT_TRUE(scheduler.register_main(controller));
        ASSERT_TRUE(scheduler.register_worker(physics));
        for (int tick = 0; tick < 3; ++tick) {
            ASSERT_TRUE(scheduler.fixed_tick(world, 1.0f / 20.0f));
        }

        const auto& player_state =
            world.registry().get<snt::player::PlayerControllerState>(player_entity);
        const auto& transform =
            world.registry().get<snt::ecs::Transform>(player_entity);
        EXPECT_TRUE(player_state.grounded);
        EXPECT_FLOAT_EQ(player_state.feet_position.y, 1.0f);
        EXPECT_FLOAT_EQ(player_state.velocity.y, 0.0f);
        EXPECT_FLOAT_EQ(transform.position[1], 1.0f + 1.62f);
        EXPECT_FLOAT_EQ(transform.rotation[0], -25.0f);
        EXPECT_FLOAT_EQ(transform.rotation[1], -90.0f);

        const auto systems = scheduler.systems();
        ASSERT_EQ(systems.size(), 2u);
        EXPECT_EQ(systems[0].metadata.name, "player.controller");
        EXPECT_FALSE(systems[0].worker);
        EXPECT_EQ(systems[1].metadata.name, "player.physics");
        EXPECT_TRUE(systems[1].worker);
        EXPECT_EQ(scheduler.diagnostics().worker_tasks_submitted, 3u);
        EXPECT_EQ(scheduler.diagnostics().commands_applied, 3u);
    }
    jobs.shutdown();
}

TEST(PlayerRayCastTest, DdaHitsExpectedVoxelAndFaceNormal) {
    snt::data::ChunkRegistry registry;
    auto chunk = make_empty_chunk();
    chunk.terrain.set_cell(3, 4, 5, 1, snt::data::TF_SOLID);
    registry.set_chunk("overworld", 0, 0, 0, std::move(chunk));

    const snt::player::CollisionWorldView world(&registry, "overworld", false);

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

    const snt::player::CollisionWorldView world(&registry, "overworld", false);

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
