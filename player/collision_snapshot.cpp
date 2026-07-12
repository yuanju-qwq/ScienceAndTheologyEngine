// VoxelCollisionSnapshot implementation.

#include "player/collision_snapshot.h"

#include <algorithm>
#include <cstdint>

namespace snt::player {
namespace {

constexpr int32_t kSnapshotGuardCells = 1;

int32_t min_swept_cell(float start, float end) {
    return floor_to_i32(std::min(start, end)) - kSnapshotGuardCells;
}

int32_t max_swept_cell(float start, float end) {
    return floor_to_i32(std::max(start, end)) + kSnapshotGuardCells;
}

}  // namespace

VoxelCollisionSnapshot VoxelCollisionSnapshot::capture(
    const CollisionWorldView& source,
    const Aabb& start_box,
    const Vec3& desired_delta) {
    VoxelCollisionSnapshot snapshot;
    const Aabb end_box = translate_aabb(start_box, desired_delta);
    snapshot.min_cell_ = {
        min_swept_cell(start_box.min.x, end_box.min.x),
        min_swept_cell(start_box.min.y, end_box.min.y),
        min_swept_cell(start_box.min.z, end_box.min.z),
    };
    snapshot.max_cell_ = {
        max_swept_cell(start_box.max.x, end_box.max.x),
        max_swept_cell(start_box.max.y, end_box.max.y),
        max_swept_cell(start_box.max.z, end_box.max.z),
    };
    snapshot.out_of_bounds_solid_ = source.missing_chunks_are_solid;

    const int64_t size_x = static_cast<int64_t>(snapshot.max_cell_.x) -
                           static_cast<int64_t>(snapshot.min_cell_.x) + 1;
    const int64_t size_y = static_cast<int64_t>(snapshot.max_cell_.y) -
                           static_cast<int64_t>(snapshot.min_cell_.y) + 1;
    const int64_t size_z = static_cast<int64_t>(snapshot.max_cell_.z) -
                           static_cast<int64_t>(snapshot.min_cell_.z) + 1;
    const size_t cell_count = static_cast<size_t>(size_x * size_y * size_z);
    snapshot.solid_cells_.resize(cell_count);

    for (int32_t y = snapshot.min_cell_.y; y <= snapshot.max_cell_.y; ++y) {
        for (int32_t z = snapshot.min_cell_.z; z <= snapshot.max_cell_.z; ++z) {
            for (int32_t x = snapshot.min_cell_.x; x <= snapshot.max_cell_.x; ++x) {
                snapshot.solid_cells_[snapshot.index_of(x, y, z)] =
                    source.is_solid_block(x, y, z) ? 1u : 0u;
            }
        }
    }
    return snapshot;
}

bool VoxelCollisionSnapshot::is_solid_block(int32_t x, int32_t y, int32_t z) const {
    if (!contains(x, y, z)) {
        return out_of_bounds_solid_;
    }
    return solid_cells_[index_of(x, y, z)] != 0;
}

bool VoxelCollisionSnapshot::contains(int32_t x, int32_t y, int32_t z) const {
    return x >= min_cell_.x && x <= max_cell_.x &&
           y >= min_cell_.y && y <= max_cell_.y &&
           z >= min_cell_.z && z <= max_cell_.z;
}

size_t VoxelCollisionSnapshot::index_of(int32_t x, int32_t y, int32_t z) const {
    const size_t size_x = static_cast<size_t>(
        static_cast<int64_t>(max_cell_.x) - static_cast<int64_t>(min_cell_.x) + 1);
    const size_t size_z = static_cast<size_t>(
        static_cast<int64_t>(max_cell_.z) - static_cast<int64_t>(min_cell_.z) + 1);
    const size_t local_x = static_cast<size_t>(x - min_cell_.x);
    const size_t local_y = static_cast<size_t>(y - min_cell_.y);
    const size_t local_z = static_cast<size_t>(z - min_cell_.z);
    return (local_y * size_z + local_z) * size_x + local_x;
}

}  // namespace snt::player
