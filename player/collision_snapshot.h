// VoxelCollisionSnapshot -- value-owned terrain input for worker physics.
//
// ChunkRegistry is main-thread-only. This snapshot copies just the swept
// voxel range required by one collision integration, so a worker task can
// calculate movement without retaining a registry, World, or chunk pointer.

#pragma once

#include "player/voxel_collision.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace snt::player {

class VoxelCollisionSnapshot final : public IVoxelCollisionWorld {
public:
    // Capture the swept AABB plus a one-cell guard band from a main-thread
    // view. The returned object owns all queried solid-state bits.
    [[nodiscard]] static VoxelCollisionSnapshot capture(
        const CollisionWorldView& source,
        const Aabb& start_box,
        const Vec3& desired_delta);

    bool is_solid_block(int32_t x, int32_t y, int32_t z) const override;

private:
    [[nodiscard]] bool contains(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] size_t index_of(int32_t x, int32_t y, int32_t z) const;

    IVec3 min_cell_{};
    IVec3 max_cell_{-1, -1, -1};
    std::vector<uint8_t> solid_cells_;
    bool out_of_bounds_solid_ = false;
};

}  // namespace snt::player
