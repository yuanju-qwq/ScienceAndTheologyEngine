#include "ship_local_grid.h"

namespace snt::data {

ShipLocalGrid::ShipLocalGrid(DynamicStructureEntity& entity)
    : entity_(entity) {
    rebuild_index();
}

void ShipLocalGrid::rebuild_index() {
    index_.clear();
    const auto& blocks = entity_.snapshot.blocks;
    index_.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const LocalStructureBlock& block = blocks[i];
        index_[BlockPos3i{block.local_x, block.local_y, block.local_z}] = i;
    }
}

const TerrainCell* ShipLocalGrid::get_cell(
    int32_t local_x, int32_t local_y, int32_t local_z) const {
    auto it = index_.find(BlockPos3i{local_x, local_y, local_z});
    if (it == index_.end()) return nullptr;
    const LocalStructureBlock& block = entity_.snapshot.blocks[it->second];
    scratch_.material = block.material;
    scratch_.flags = block.flags;
    scratch_.clear_fluid();
    return &scratch_;
}

bool ShipLocalGrid::set_cell(
    int32_t local_x, int32_t local_y, int32_t local_z,
    TerrainMaterialId material, uint32_t flags) {
    auto it = index_.find(BlockPos3i{local_x, local_y, local_z});
    if (it == index_.end()) return false;
    LocalStructureBlock& block = entity_.snapshot.blocks[it->second];
    block.material = material;
    block.flags = flags;
    ++entity_.structure_version;
    entity_.dirty_mesh = true;
    return true;
}

size_t ShipLocalGrid::block_count() const {
    return entity_.snapshot.block_count();
}

uint32_t ShipLocalGrid::structure_version() const {
    return entity_.structure_version;
}

} // namespace snt::data
