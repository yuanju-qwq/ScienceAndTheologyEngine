#pragma once

#include "player/voxel_collision.h"

namespace snt::player {

struct RayCastResult {
    bool hit = false;
    IVec3 block{};
    IVec3 previous{};
    IVec3 normal{};
    float distance = 0.0f;
};

RayCastResult ray_cast_voxels_dda(
    const CollisionWorldView& world,
    Vec3 origin,
    Vec3 direction,
    float max_distance);

}  // namespace snt::player
