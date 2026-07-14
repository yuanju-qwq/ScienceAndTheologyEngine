#include "voxel/storage/region_compactor.h"

#include "voxel/storage/region_file.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <system_error>
#include <vector>

namespace snt::voxel {
namespace {

namespace fs = std::filesystem;

fs::path utf8_path(const std::string& path) {
    return fs::u8path(path);
}

std::string path_to_utf8(const fs::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::string region_file_path(const std::string& world_dir,
                             const std::string& dimension_id,
                             int region_x, int region_y, int region_z) {
    return world_dir + "/regions/" + VoxelRegionFile::region_file_name(
        dimension_id, region_x, region_y, region_z);
}

int local_key(const VoxelRegionEntry& entry) {
    return (static_cast<int>(entry.local_x) << 16)
        | (static_cast<int>(entry.local_y) << 8)
        | static_cast<int>(entry.local_z);
}

std::vector<VoxelRegionEntry> deduplicate_keep_last(
    const std::vector<VoxelRegionEntry>& entries, int32_t& duplicates_removed) {
    duplicates_removed = 0;
    std::map<int, size_t> latest_index_by_local;
    for (size_t index = 0; index < entries.size(); ++index) {
        const int key = local_key(entries[index]);
        const auto [iterator, inserted] = latest_index_by_local.emplace(key, index);
        if (!inserted) {
            ++duplicates_removed;
            iterator->second = index;
        }
    }

    std::vector<size_t> kept_indices;
    kept_indices.reserve(latest_index_by_local.size());
    for (const auto& [key, index] : latest_index_by_local) {
        (void)key;
        kept_indices.push_back(index);
    }
    std::sort(kept_indices.begin(), kept_indices.end());

    std::vector<VoxelRegionEntry> compacted;
    compacted.reserve(kept_indices.size());
    for (const size_t index : kept_indices) compacted.push_back(entries[index]);
    return compacted;
}

VoxelRegionCompactionResult compact_region_file(
    const std::string& file_path,
    const std::string& expected_dimension_id,
    int expected_region_x, int expected_region_y, int expected_region_z) {
    VoxelRegionCompactionResult result;
    if (!fs::exists(utf8_path(file_path))) return result;

    result.region_files_scanned = 1;
    std::string dimension_id;
    int region_x = 0;
    int region_y = 0;
    int region_z = 0;
    std::vector<VoxelRegionEntry> entries;
    if (!VoxelRegionFile::read(file_path, dimension_id, region_x, region_y, region_z, entries) ||
        dimension_id != expected_dimension_id || region_x != expected_region_x ||
        region_y != expected_region_y || region_z != expected_region_z) {
        result.ok = false;
        result.failed_region_files = 1;
        return result;
    }

    result.chunk_entries_before = static_cast<int32_t>(entries.size());
    std::vector<VoxelRegionEntry> compacted =
        deduplicate_keep_last(entries, result.duplicate_entries_removed);
    result.chunk_entries_after = static_cast<int32_t>(compacted.size());
    if (!VoxelRegionFile::write(file_path, region_x, region_y, region_z, dimension_id, compacted)) {
        result.ok = false;
        result.failed_region_files = 1;
        return result;
    }

    result.region_files_compacted = 1;
    return result;
}

void accumulate(VoxelRegionCompactionResult& total,
                const VoxelRegionCompactionResult& part) {
    total.ok = total.ok && part.ok;
    total.region_files_scanned += part.region_files_scanned;
    total.region_files_compacted += part.region_files_compacted;
    total.chunk_entries_before += part.chunk_entries_before;
    total.chunk_entries_after += part.chunk_entries_after;
    total.duplicate_entries_removed += part.duplicate_entries_removed;
    total.failed_region_files += part.failed_region_files;
}

}  // namespace

VoxelRegionCompactionResult VoxelRegionCompactor::compact_region(
    const std::string& world_dir,
    const std::string& dimension_id,
    int region_x, int region_y, int region_z) {
    return compact_region_file(
        region_file_path(world_dir, dimension_id, region_x, region_y, region_z),
        dimension_id, region_x, region_y, region_z);
}

VoxelRegionCompactionResult VoxelRegionCompactor::compact_dimension(
    const std::string& world_dir,
    const std::string& dimension_id) {
    VoxelRegionCompactionResult total;
    const fs::path regions_path = utf8_path(world_dir + "/regions");
    if (!fs::exists(regions_path) || !fs::is_directory(regions_path)) return total;

    for (const auto& entry : fs::directory_iterator(regions_path)) {
        if (!entry.is_regular_file()) continue;

        const std::string file_path = path_to_utf8(entry.path());
        std::string file_dimension_id;
        int region_x = 0;
        int region_y = 0;
        int region_z = 0;
        std::vector<VoxelRegionEntry> entries;
        if (!VoxelRegionFile::read(file_path, file_dimension_id, region_x, region_y, region_z, entries)) {
            VoxelRegionCompactionResult failed;
            failed.ok = false;
            failed.region_files_scanned = 1;
            failed.failed_region_files = 1;
            accumulate(total, failed);
            continue;
        }
        if (file_dimension_id != dimension_id) continue;

        accumulate(total, compact_region_file(
            file_path, dimension_id, region_x, region_y, region_z));
    }

    return total;
}

}  // namespace snt::voxel
