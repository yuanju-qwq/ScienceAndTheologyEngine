#include "player/ray_cast.h"

#include <array>
#include <cmath>
#include <limits>

namespace snt::player {
namespace {

float axis_t_max(float origin, int32_t block, int32_t step, float dir) {
    if (dir == 0.0f) {
        return std::numeric_limits<float>::infinity();
    }
    const float boundary = step > 0 ? static_cast<float>(block + 1)
                                    : static_cast<float>(block);
    return (boundary - origin) / dir;
}

float axis_t_delta(float dir) {
    if (dir == 0.0f) {
        return std::numeric_limits<float>::infinity();
    }
    return std::abs(1.0f / dir);
}

bool nearly_equal(float a, float b) {
    constexpr float kEpsilon = 1.0e-6f;
    return std::abs(a - b) <= kEpsilon;
}

}  // namespace

RayCastResult ray_cast_voxels_dda(
        const CollisionWorldView& world,
        Vec3 origin,
        Vec3 direction,
        float max_distance) {
    RayCastResult result;

    const float len_sq = direction.x * direction.x
        + direction.y * direction.y
        + direction.z * direction.z;
    if (len_sq <= 0.000001f || max_distance <= 0.0f) {
        return result;
    }

    const float inv_len = 1.0f / std::sqrt(len_sq);
    direction.x *= inv_len;
    direction.y *= inv_len;
    direction.z *= inv_len;

    IVec3 block{
        floor_to_i32(origin.x),
        floor_to_i32(origin.y),
        floor_to_i32(origin.z),
    };
    IVec3 previous = block;

    const IVec3 step{
        direction.x > 0.0f ? 1 : -1,
        direction.y > 0.0f ? 1 : -1,
        direction.z > 0.0f ? 1 : -1,
    };

    Vec3 t_max{
        axis_t_max(origin.x, block.x, step.x, direction.x),
        axis_t_max(origin.y, block.y, step.y, direction.y),
        axis_t_max(origin.z, block.z, step.z, direction.z),
    };
    Vec3 t_delta{
        axis_t_delta(direction.x),
        axis_t_delta(direction.y),
        axis_t_delta(direction.z),
    };

    if (world.is_solid_block(block.x, block.y, block.z)) {
        result.hit = true;
        result.block = block;
        result.previous = block;
        result.normal = {0, 0, 0};
        result.distance = 0.0f;
        return result;
    }

    float distance = 0.0f;
    IVec3 normal{};

    while (distance <= max_distance) {
        previous = block;

        const float next_distance = std::min(t_max.x, std::min(t_max.y, t_max.z));
        if (!std::isfinite(next_distance)) {
            break;
        }
        distance = next_distance;

        if (distance > max_distance) {
            break;
        }

        const bool cross_x = nearly_equal(t_max.x, next_distance);
        const bool cross_y = nearly_equal(t_max.y, next_distance);
        const bool cross_z = nearly_equal(t_max.z, next_distance);

        const IVec3 full_step{
            cross_x ? step.x : 0,
            cross_y ? step.y : 0,
            cross_z ? step.z : 0,
        };

        std::array<int, 7> masks{};
        int mask_count = 0;
        int full_mask = 0;
        if (cross_x) full_mask |= 1;
        if (cross_y) full_mask |= 2;
        if (cross_z) full_mask |= 4;
        masks[mask_count++] = full_mask;
        for (int mask = 1; mask <= 7; ++mask) {
            if (mask == full_mask || (mask & full_mask) != mask) {
                continue;
            }
            masks[mask_count++] = mask;
        }

        for (int i = 0; i < mask_count; ++i) {
            const int mask = masks[i];
            const IVec3 candidate{
                previous.x + ((mask & 1) ? step.x : 0),
                previous.y + ((mask & 2) ? step.y : 0),
                previous.z + ((mask & 4) ? step.z : 0),
            };
            normal = {
                (mask & 1) ? -step.x : 0,
                (mask & 2) ? -step.y : 0,
                (mask & 4) ? -step.z : 0,
            };
            if (world.is_solid_block(candidate.x, candidate.y, candidate.z)) {
                result.hit = true;
                result.block = candidate;
                result.previous = {
                    candidate.x + normal.x,
                    candidate.y + normal.y,
                    candidate.z + normal.z,
                };
                result.normal = normal;
                result.distance = distance;
                return result;
            }
        }

        block.x += full_step.x;
        block.y += full_step.y;
        block.z += full_step.z;
        if (cross_x) t_max.x += t_delta.x;
        if (cross_y) t_max.y += t_delta.y;
        if (cross_z) t_max.z += t_delta.z;
    }

    return result;
}

}  // namespace snt::player
