#pragma once

#include "data/defs/chunk_data.h"

#include <cstdint>
#include <string>

namespace snt::data { class ChunkRegistry; }

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

struct CollisionWorldView {
    const snt::data::ChunkRegistry* chunks = nullptr;
    std::string dimension_id = "overworld";
    bool missing_chunks_are_solid = false;

    bool is_solid_block(int32_t x, int32_t y, int32_t z) const;
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
bool aabb_overlaps_solid_voxels(const CollisionWorldView& world, const Aabb& box);

CollisionMoveResult move_aabb_collide_voxels(
    const CollisionWorldView& world,
    const Aabb& start_box,
    const Vec3& desired_delta);

}  // namespace snt::player
