#define SNT_LOG_CHANNEL "player"
#include "core/log.h"

#include "player/voxel_collision.h"

#include "data/defs/terrain_data.h"
#include "data/world/chunk_registry.h"

#include <algorithm>
#include <cmath>

namespace snt::player {
namespace {

constexpr float kEpsilon = 0.0001f;
constexpr int32_t kChunkSize = snt::data::ChunkData::kChunkSize;

bool range_has_solid(const CollisionWorldView& world,
                     int32_t min_x, int32_t max_x,
                     int32_t min_y, int32_t max_y,
                     int32_t min_z, int32_t max_z) {
    for (int32_t y = min_y; y <= max_y; ++y) {
        for (int32_t z = min_z; z <= max_z; ++z) {
            for (int32_t x = min_x; x <= max_x; ++x) {
                if (world.is_solid_block(x, y, z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

int32_t floor_to_i32(float v) {
    return static_cast<int32_t>(std::floor(v));
}

int32_t floor_div_i32(int32_t value, int32_t divisor) {
    int32_t q = value / divisor;
    int32_t r = value % divisor;
    if (r != 0 && ((r < 0) != (divisor < 0))) {
        --q;
    }
    return q;
}

int32_t positive_mod_i32(int32_t value, int32_t divisor) {
    int32_t r = value % divisor;
    if (r < 0) {
        r += divisor;
    }
    return r;
}

bool CollisionWorldView::is_solid_block(int32_t x, int32_t y, int32_t z) const {
    if (!chunks) {
        return missing_chunks_are_solid;
    }

    const int32_t cx = floor_div_i32(x, kChunkSize);
    const int32_t cy = floor_div_i32(y, kChunkSize);
    const int32_t cz = floor_div_i32(z, kChunkSize);
    const int32_t lx = positive_mod_i32(x, kChunkSize);
    const int32_t ly = positive_mod_i32(y, kChunkSize);
    const int32_t lz = positive_mod_i32(z, kChunkSize);

    const snt::data::ChunkData* chunk =
        chunks->get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) {
        return missing_chunks_are_solid;
    }
    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) {
        return missing_chunks_are_solid;
    }

    return chunk->terrain.cell_at(lx, ly, lz).is_solid();
}

Aabb translate_aabb(const Aabb& box, const Vec3& delta) {
    return Aabb{
        .min = {box.min.x + delta.x, box.min.y + delta.y, box.min.z + delta.z},
        .max = {box.max.x + delta.x, box.max.y + delta.y, box.max.z + delta.z},
    };
}

bool aabb_overlaps_solid_voxels(const CollisionWorldView& world, const Aabb& box) {
    const int32_t min_x = floor_to_i32(box.min.x);
    const int32_t min_y = floor_to_i32(box.min.y);
    const int32_t min_z = floor_to_i32(box.min.z);
    const int32_t max_x = floor_to_i32(box.max.x - kEpsilon);
    const int32_t max_y = floor_to_i32(box.max.y - kEpsilon);
    const int32_t max_z = floor_to_i32(box.max.z - kEpsilon);

    return range_has_solid(world, min_x, max_x, min_y, max_y, min_z, max_z);
}

CollisionMoveResult move_aabb_collide_voxels(
        const CollisionWorldView& world,
        const Aabb& start_box,
        const Vec3& desired_delta) {
    CollisionMoveResult result;
    result.delta = desired_delta;

    Aabb current = start_box;

    if (result.delta.x != 0.0f) {
        Aabb candidate = translate_aabb(current, {result.delta.x, 0.0f, 0.0f});
        const int32_t min_y = floor_to_i32(candidate.min.y);
        const int32_t max_y = floor_to_i32(candidate.max.y - kEpsilon);
        const int32_t min_z = floor_to_i32(candidate.min.z);
        const int32_t max_z = floor_to_i32(candidate.max.z - kEpsilon);

        if (result.delta.x > 0.0f) {
            const int32_t block_x = floor_to_i32(candidate.max.x - kEpsilon);
            if (range_has_solid(world, block_x, block_x, min_y, max_y, min_z, max_z)) {
                result.delta.x = std::max(0.0f, static_cast<float>(block_x) - current.max.x);
                result.hit_x = true;
            }
        } else {
            const int32_t block_x = floor_to_i32(candidate.min.x);
            if (range_has_solid(world, block_x, block_x, min_y, max_y, min_z, max_z)) {
                result.delta.x = std::min(0.0f, static_cast<float>(block_x + 1) - current.min.x);
                result.hit_x = true;
            }
        }
        current = translate_aabb(current, {result.delta.x, 0.0f, 0.0f});
    }

    if (result.delta.y != 0.0f) {
        Aabb candidate = translate_aabb(current, {0.0f, result.delta.y, 0.0f});
        const int32_t min_x = floor_to_i32(candidate.min.x);
        const int32_t max_x = floor_to_i32(candidate.max.x - kEpsilon);
        const int32_t min_z = floor_to_i32(candidate.min.z);
        const int32_t max_z = floor_to_i32(candidate.max.z - kEpsilon);

        if (result.delta.y > 0.0f) {
            const int32_t block_y = floor_to_i32(candidate.max.y - kEpsilon);
            if (range_has_solid(world, min_x, max_x, block_y, block_y, min_z, max_z)) {
                result.delta.y = std::max(0.0f, static_cast<float>(block_y) - current.max.y);
                result.hit_y = true;
            }
        } else {
            const int32_t block_y = floor_to_i32(candidate.min.y);
            if (range_has_solid(world, min_x, max_x, block_y, block_y, min_z, max_z)) {
                result.delta.y = std::min(0.0f, static_cast<float>(block_y + 1) - current.min.y);
                result.hit_y = true;
                result.grounded = true;
            }
        }
        current = translate_aabb(current, {0.0f, result.delta.y, 0.0f});
    }

    if (result.delta.z != 0.0f) {
        Aabb candidate = translate_aabb(current, {0.0f, 0.0f, result.delta.z});
        const int32_t min_x = floor_to_i32(candidate.min.x);
        const int32_t max_x = floor_to_i32(candidate.max.x - kEpsilon);
        const int32_t min_y = floor_to_i32(candidate.min.y);
        const int32_t max_y = floor_to_i32(candidate.max.y - kEpsilon);

        if (result.delta.z > 0.0f) {
            const int32_t block_z = floor_to_i32(candidate.max.z - kEpsilon);
            if (range_has_solid(world, min_x, max_x, min_y, max_y, block_z, block_z)) {
                result.delta.z = std::max(0.0f, static_cast<float>(block_z) - current.max.z);
                result.hit_z = true;
            }
        } else {
            const int32_t block_z = floor_to_i32(candidate.min.z);
            if (range_has_solid(world, min_x, max_x, min_y, max_y, block_z, block_z)) {
                result.delta.z = std::min(0.0f, static_cast<float>(block_z + 1) - current.min.z);
                result.hit_z = true;
            }
        }
    }

    return result;
}

}  // namespace snt::player
