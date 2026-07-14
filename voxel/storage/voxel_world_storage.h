// Future-facing raw voxel world storage contract.
//
// A game serializer produces opaque payloads, while this engine boundary owns
// region addressing and raw payload persistence. No gameplay state crosses
// this interface.

#pragma once

#include "voxel/storage/region_file.h"

#include <string>
#include <vector>

namespace snt::voxel {

struct VoxelRegionAddress {
    std::string dimension_id;
    int region_x = 0;
    int region_y = 0;
    int region_z = 0;
};

class IVoxelWorldStorage {
public:
    virtual ~IVoxelWorldStorage() = default;

    virtual bool write_region(const VoxelRegionAddress& address,
                              const std::vector<VoxelRegionEntry>& entries) = 0;
    virtual bool read_region(const VoxelRegionAddress& address,
                             std::vector<VoxelRegionEntry>& out_entries) = 0;
};

}  // namespace snt::voxel
