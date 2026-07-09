#pragma once

#include <cstdint>
#include <unordered_map>

#include "dynamic_structure.h"

namespace snt::data {

// Local-grid read/write adapter for a DynamicStructureEntity. This is the
// future bridge that lets GT/AE2 systems query a ship in local coordinates
// without pretending the ship is still world terrain while it is moving.
class ShipLocalGrid {
public:
    explicit ShipLocalGrid(DynamicStructureEntity& entity);

    const TerrainCell* get_cell(int32_t local_x, int32_t local_y, int32_t local_z) const;
    bool set_cell(int32_t local_x, int32_t local_y, int32_t local_z,
                  TerrainMaterialId material, uint32_t flags);

    size_t block_count() const;
    uint32_t structure_version() const;

private:
    void rebuild_index();

    DynamicStructureEntity& entity_;
    std::unordered_map<BlockPos3i, size_t, BlockPos3iHash> index_;
    mutable TerrainCell scratch_{};
};

} // namespace snt::data
