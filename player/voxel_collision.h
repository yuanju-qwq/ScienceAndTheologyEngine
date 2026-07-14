#pragma once

#include "voxel/data/voxel_chunk.h"

#include <cstdint>
#include <string>
#include <utility>

namespace snt::voxel { class ChunkRegistry; }

namespace snt::player {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct IVec3 {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

struct Aabb {
    Vec3 min;
    Vec3 max;
};

// Read-only voxel query boundary shared by main-thread world views and
// value-owned worker snapshots. Physics and ray casts depend on this narrow
// interface instead of retaining a ChunkRegistry reference across threads.
class IVoxelCollisionWorld {
public:
    virtual ~IVoxelCollisionWorld() = default;
    virtual bool is_solid_block(int32_t x, int32_t y, int32_t z) const = 0;
};

// Main-thread adapter over ChunkRegistry. It is intentionally not safe to
// pass to a worker; VoxelCollisionSnapshot is the worker-safe implementation.
struct CollisionWorldView final : IVoxelCollisionWorld {
    CollisionWorldView(const snt::voxel::ChunkRegistry* chunk_registry = nullptr,
                       std::string world_dimension_id = "overworld",
                       bool missing_are_solid = false)
        : chunks(chunk_registry),
          dimension_id(std::move(world_dimension_id)),
          missing_chunks_are_solid(missing_are_solid) {}

    const snt::voxel::ChunkRegistry* chunks = nullptr;
    std::string dimension_id = "overworld";
    bool missing_chunks_are_solid = false;

    bool is_solid_block(int32_t x, int32_t y, int32_t z) const override;
};

struct CollisionMoveResult {
    Vec3 delta;
    bool hit_x = false;
    bool hit_y = false;
    bool hit_z = false;
    bool grounded = false;
};

int32_t floor_to_i32(float v);
int32_t floor_div_i32(int32_t value, int32_t divisor);
int32_t positive_mod_i32(int32_t value, int32_t divisor);

Aabb translate_aabb(const Aabb& box, const Vec3& delta);
bool aabb_overlaps_solid_voxels(const IVoxelCollisionWorld& world, const Aabb& box);

CollisionMoveResult move_aabb_collide_voxels(
    const IVoxelCollisionWorld& world,
    const Aabb& start_box,
    const Vec3& desired_delta);

}  // namespace snt::player
