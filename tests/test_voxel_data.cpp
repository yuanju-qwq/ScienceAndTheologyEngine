// Tests for engine-owned generic voxel data and raw region framing.

#include "voxel/data/chunk_registry.h"
#include "voxel/storage/region_file.h"
#include "voxel/storage/voxel_world_storage.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

class MemoryVoxelWorldStorage final : public snt::voxel::IVoxelWorldStorage {
public:
    bool write_region(const snt::voxel::VoxelRegionAddress& address,
                      const std::vector<snt::voxel::VoxelRegionEntry>& entries) override {
        entries_[key(address)] = entries;
        return true;
    }

    bool read_region(const snt::voxel::VoxelRegionAddress& address,
                     std::vector<snt::voxel::VoxelRegionEntry>& out_entries) override {
        const auto it = entries_.find(key(address));
        if (it == entries_.end()) return false;
        out_entries = it->second;
        return true;
    }

private:
    static std::string key(const snt::voxel::VoxelRegionAddress& address) {
        return address.dimension_id + ":" + std::to_string(address.region_x) + ":" +
            std::to_string(address.region_y) + ":" + std::to_string(address.region_z);
    }

    std::unordered_map<std::string, std::vector<snt::voxel::VoxelRegionEntry>> entries_;
};

}  // namespace

TEST(VoxelChunkRegistryTest, StoresOnlyGenericVoxelChunks) {
    snt::voxel::ChunkRegistry registry;
    snt::voxel::VoxelChunk chunk;
    chunk.terrain.resize(8, 8, 8);
    chunk.terrain.set_cell(1, 2, 3, 7, snt::voxel::TF_SOLID);

    registry.set_chunk("overworld", 1, 2, 3, std::move(chunk));

    const auto* stored = registry.get_chunk("overworld", 1, 2, 3);
    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->terrain.cell_at(1, 2, 3).is_solid());
    EXPECT_EQ(registry.chunk_count(), 1u);
}

TEST(VoxelChunkRegistryTest, KeepsDimensionsIndependentAndSupportsRemoval) {
    snt::voxel::ChunkRegistry registry;
    registry.set_chunk("overworld", 0, 0, 0, {});
    registry.set_chunk("nether", 0, 0, 0, {});

    EXPECT_NE(registry.get_chunk("overworld", 0, 0, 0), nullptr);
    EXPECT_NE(registry.get_chunk("nether", 0, 0, 0), nullptr);
    EXPECT_EQ(registry.all_chunk_keys().size(), 2u);

    registry.remove_chunk("overworld", 0, 0, 0);
    EXPECT_FALSE(registry.has_chunk("overworld", 0, 0, 0));
    EXPECT_TRUE(registry.has_chunk("nether", 0, 0, 0));
}

TEST(VoxelRegionFileTest, RoundTripsOpaquePayloads) {
    const std::string file_path =
        (std::filesystem::temp_directory_path() / "snt_voxel_region_test.region").string();
    const std::vector<snt::voxel::VoxelRegionEntry> entries{
        {.local_x = 0, .local_y = 1, .local_z = 2, .payload = {1, 2, 3}},
        {.local_x = 31, .local_y = 0, .local_z = 4, .payload = {4, 5}},
    };

    ASSERT_TRUE(snt::voxel::VoxelRegionFile::write(
        file_path, -1, 0, 2, "overworld", entries));

    std::string dimension;
    int region_x = 0;
    int region_y = 0;
    int region_z = 0;
    std::vector<snt::voxel::VoxelRegionEntry> restored;
    ASSERT_TRUE(snt::voxel::VoxelRegionFile::read(
        file_path, dimension, region_x, region_y, region_z, restored));

    EXPECT_EQ(dimension, "overworld");
    EXPECT_EQ(region_x, -1);
    ASSERT_EQ(restored.size(), entries.size());
    EXPECT_EQ(restored[1].payload, entries[1].payload);
    std::filesystem::remove(file_path);
}

TEST(VoxelRegionFileTest, MapsNegativeChunkCoordinates) {
    EXPECT_EQ(snt::voxel::VoxelRegionFile::to_region(31), 0);
    EXPECT_EQ(snt::voxel::VoxelRegionFile::to_region(32), 1);
    EXPECT_EQ(snt::voxel::VoxelRegionFile::to_region(-1), -1);
    EXPECT_EQ(snt::voxel::VoxelRegionFile::to_region(-32), -1);
    EXPECT_EQ(snt::voxel::VoxelRegionFile::to_local(-1), 31);
    EXPECT_EQ(snt::voxel::VoxelRegionFile::to_local(32), 0);
}

TEST(VoxelWorldStorageContractTest, AcceptsOpaqueRegionEntries) {
    MemoryVoxelWorldStorage storage;
    const snt::voxel::VoxelRegionAddress address{
        .dimension_id = "overworld",
        .region_x = 1,
        .region_y = 2,
        .region_z = 3,
    };
    const std::vector<snt::voxel::VoxelRegionEntry> expected{
        {.local_x = 2, .local_y = 3, .local_z = 4, .payload = {9, 8, 7}},
    };

    ASSERT_TRUE(storage.write_region(address, expected));
    std::vector<snt::voxel::VoxelRegionEntry> restored;
    ASSERT_TRUE(storage.read_region(address, restored));
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored.front().payload, expected.front().payload);
}
