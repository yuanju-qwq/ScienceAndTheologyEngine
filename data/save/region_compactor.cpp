#include "region_compactor.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <system_error>
#include <vector>

#include "region_file.h"

namespace fs = std::filesystem;

namespace snt::data {
namespace {

fs::path utf8_path(const std::string& path) {
    return fs::u8path(path);
}

std::string path_to_utf8(const fs::path& path) {
    const auto encoded = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::string region_file_path(const std::string& planet_dir,
                             const std::string& dimension_id,
                             int region_x, int region_y, int region_z) {
    return planet_dir + "/regions/" + RegionFile::region_file_name(
        dimension_id, region_x, region_y, region_z);
}

int local_key(const RegionChunkEntry& entry) {
    return (static_cast<int>(entry.local_x) << 16)
        | (static_cast<int>(entry.local_y) << 8)
        | static_cast<int>(entry.local_z);
}

std::vector<RegionChunkEntry> deduplicate_keep_last(
        const std::vector<RegionChunkEntry>& entries,
        int32_t& duplicates_removed) {
    duplicates_removed = 0;
    std::map<int, size_t> latest_index_by_local;
    for (size_t i = 0; i < entries.size(); ++i) {
        const int key = local_key(entries[i]);
        auto it = latest_index_by_local.find(key);
        if (it != latest_index_by_local.end()) {
            ++duplicates_removed;
            it->second = i;
        } else {
            latest_index_by_local[key] = i;
        }
    }

    std::vector<size_t> kept_indices;
    kept_indices.reserve(latest_index_by_local.size());
    for (const auto& pair : latest_index_by_local) {
        kept_indices.push_back(pair.second);
    }
    std::sort(kept_indices.begin(), kept_indices.end());

    std::vector<RegionChunkEntry> compacted;
    compacted.reserve(kept_indices.size());
    for (size_t index : kept_indices) {
        compacted.push_back(entries[index]);
    }
    return compacted;
}

RegionCompactionResult compact_region_file(
        const std::string& file_path,
        const std::string& expected_dimension_id,
        int expected_region_x,
        int expected_region_y,
        int expected_region_z) {
    RegionCompactionResult result;
    const fs::path path = utf8_path(file_path);
    if (!fs::exists(path)) {
        return result;
    }

    result.region_files_scanned = 1;

    std::string file_dimension_id;
    int region_x = 0;
    int region_y = 0;
    int region_z = 0;
    std::vector<RegionChunkEntry> entries;
    if (!RegionFile::read(file_path, file_dimension_id,
                          region_x, region_y, region_z, entries)) {
        result.ok = false;
        result.failed_region_files = 1;
        return result;
    }

    if (file_dimension_id != expected_dimension_id
            || region_x != expected_region_x
            || region_y != expected_region_y
            || region_z != expected_region_z) {
        result.ok = false;
        result.failed_region_files = 1;
        return result;
    }

    result.chunk_entries_before = static_cast<int32_t>(entries.size());

    int32_t duplicates_removed = 0;
    std::vector<RegionChunkEntry> compacted =
        deduplicate_keep_last(entries, duplicates_removed);

    result.chunk_entries_after = static_cast<int32_t>(compacted.size());
    result.duplicate_entries_removed = duplicates_removed;

    // Rewriting even an already unique region normalizes the file layout and
    // compacts any previous over-allocation/fragmentation in the region file.
    if (!RegionFile::write(file_path, region_x, region_y, region_z,
                           file_dimension_id, compacted)) {
        result.ok = false;
        result.failed_region_files = 1;
        return result;
    }

    result.region_files_compacted = 1;
    return result;
}

void accumulate(RegionCompactionResult& total,
                const RegionCompactionResult& part) {
    total.ok = total.ok && part.ok;
    total.region_files_scanned += part.region_files_scanned;
    total.region_files_compacted += part.region_files_compacted;
    total.chunk_entries_before += part.chunk_entries_before;
    total.chunk_entries_after += part.chunk_entries_after;
    total.duplicate_entries_removed += part.duplicate_entries_removed;
    total.failed_region_files += part.failed_region_files;
}

} // namespace

RegionCompactionResult RegionCompactor::compact_region(
        const std::string& planet_dir,
        const std::string& dimension_id,
        int region_x, int region_y, int region_z) {
    const std::string file_path = region_file_path(
        planet_dir, dimension_id, region_x, region_y, region_z);
    return compact_region_file(
        file_path, dimension_id, region_x, region_y, region_z);
}

RegionCompactionResult RegionCompactor::compact_dimension(
        const std::string& planet_dir,
        const std::string& dimension_id) {
    RegionCompactionResult total;
    const fs::path regions_path = utf8_path(planet_dir + "/regions");
    if (!fs::exists(regions_path) || !fs::is_directory(regions_path)) {
        return total;
    }

    for (const auto& entry : fs::directory_iterator(regions_path)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string file_path = path_to_utf8(entry.path());
        std::string file_dimension_id;
        int region_x = 0;
        int region_y = 0;
        int region_z = 0;
        std::vector<RegionChunkEntry> entries;
        if (!RegionFile::read(file_path, file_dimension_id,
                              region_x, region_y, region_z, entries)) {
            RegionCompactionResult failed;
            failed.ok = false;
            failed.region_files_scanned = 1;
            failed.failed_region_files = 1;
            accumulate(total, failed);
            continue;
        }

        if (file_dimension_id != dimension_id) {
            continue;
        }

        RegionCompactionResult part = compact_region_file(
            file_path, dimension_id, region_x, region_y, region_z);
        accumulate(total, part);
    }

    return total;
}

} // namespace science_and_theology
