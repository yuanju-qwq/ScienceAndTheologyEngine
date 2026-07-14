// Current-format raw voxel region framing.
//
// Region files store opaque per-chunk payload blobs. The engine owns the
// framing, indexing, validation, and compaction; each game owns the payload
// schema and its serializer.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace snt::voxel {

struct VoxelRegionEntry {
    uint8_t local_x = 0;
    uint8_t local_y = 0;
    uint8_t local_z = 0;
    std::vector<uint8_t> payload;
};

class VoxelRegionFile {
public:
    static constexpr uint8_t kCurrentVersion = 2;
    static constexpr int kRegionSize = 32;

    static bool write(const std::string& file_path,
                      int region_x, int region_y, int region_z,
                      const std::string& dimension_id,
                      const std::vector<VoxelRegionEntry>& entries);

    static bool read(const std::string& file_path,
                     std::string& out_dimension_id,
                     int& out_region_x, int& out_region_y, int& out_region_z,
                     std::vector<VoxelRegionEntry>& out_entries);

    static int to_region(int chunk_coord) {
        return chunk_coord >= 0
            ? chunk_coord / kRegionSize
            : (chunk_coord - kRegionSize + 1) / kRegionSize;
    }

    static int to_local(int chunk_coord) {
        const int local = chunk_coord % kRegionSize;
        return local < 0 ? local + kRegionSize : local;
    }

    static std::string region_file_name(const std::string& dimension_id,
                                        int region_x, int region_y, int region_z);
};

}  // namespace snt::voxel
