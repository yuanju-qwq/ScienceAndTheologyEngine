// MachineCollisionOverlay implementation.
//
// Ported from src/core/world/machine_collision_overlay.cpp.
// Namespace: science_and_theology -> snt::data.

#include "defs/machine_collision_overlay.h"

#include <algorithm>

namespace snt::data {

void MachineCollisionOverlay::set(
        const std::string& dimension_id,
        int32_t cell_x, int32_t cell_y, int32_t cell_z,
        bool occupied) {
    MachineCellKey key(dimension_id, cell_x, cell_y, cell_z);
    if (occupied) {
        cells_.insert(std::move(key));
    } else {
        cells_.erase(key);
    }
}

bool MachineCollisionOverlay::is_occupied(
        const std::string& dimension_id,
        int32_t cell_x, int32_t cell_y, int32_t cell_z) const {
    MachineCellKey key(dimension_id, cell_x, cell_y, cell_z);
    return cells_.find(key) != cells_.end();
}

size_t MachineCollisionOverlay::clear_dimension(
        const std::string& dimension_id) {
    size_t removed = 0;
    for (auto it = cells_.begin(); it != cells_.end();) {
        if (it->dimension_id == dimension_id) {
            it = cells_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void MachineCollisionOverlay::clear_all() {
    cells_.clear();
}

std::vector<uint8_t> MachineCollisionOverlay::get_chunk_mask(
        const std::string& dimension_id,
        int32_t chunk_x, int32_t chunk_y, int32_t chunk_z,
        int32_t size_x, int32_t size_y, int32_t size_z) const {
    const int64_t total = static_cast<int64_t>(size_x)
                        * static_cast<int64_t>(size_y)
                        * static_cast<int64_t>(size_z);
    std::vector<uint8_t> mask;
    if (total <= 0) {
        return mask;
    }
    mask.assign(static_cast<size_t>(total), 0);

    const int32_t origin_x = chunk_x * size_x;
    const int32_t origin_y = chunk_y * size_y;
    const int32_t origin_z = chunk_z * size_z;

    // Iterate only the cells in the overlay; cells outside this chunk's bounds
    // are skipped. This keeps the per-chunk cost proportional to the number of
    // machines inside the chunk rather than the chunk volume.
    for (const auto& key : cells_) {
        if (key.dimension_id != dimension_id) {
            continue;
        }
        const int32_t lx = key.cell_x - origin_x;
        if (lx < 0 || lx >= size_x) continue;
        const int32_t ly = key.cell_y - origin_y;
        if (ly < 0 || ly >= size_y) continue;
        const int32_t lz = key.cell_z - origin_z;
        if (lz < 0 || lz >= size_z) continue;

        // Match terrain_index ordering:
        //   idx = (ly * size_z + lz) * size_x + lx
        const int64_t idx = (static_cast<int64_t>(ly) * size_z + lz) * size_x + lx;
        if (idx >= 0 && idx < total) {
            mask[static_cast<size_t>(idx)] = 1;
        }
    }
    return mask;
}

} // namespace snt::data
