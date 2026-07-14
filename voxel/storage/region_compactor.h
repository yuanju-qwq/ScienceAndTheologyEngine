// Generic raw-region compaction contract.

#pragma once

#include <cstdint>
#include <string>

namespace snt::voxel {

struct VoxelRegionCompactionResult {
    bool ok = true;
    int32_t region_files_scanned = 0;
    int32_t region_files_compacted = 0;
    int32_t chunk_entries_before = 0;
    int32_t chunk_entries_after = 0;
    int32_t duplicate_entries_removed = 0;
    int32_t failed_region_files = 0;
};

class VoxelRegionCompactor {
public:
    static VoxelRegionCompactionResult compact_region(
        const std::string& world_dir,
        const std::string& dimension_id,
        int region_x, int region_y, int region_z);

    static VoxelRegionCompactionResult compact_dimension(
        const std::string& world_dir,
        const std::string& dimension_id);
};

}  // namespace snt::voxel
