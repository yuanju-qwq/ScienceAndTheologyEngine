#include "save_manager.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <map>

#include "chunk_serializer.h"

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

bool ensure_planet_header_if_missing(const std::string& planet_dir,
                                     int64_t seed,
                                     const std::string& dimension_id) {
    const std::string path = planet_dir + "/planet_data.bin";
    if (fs::exists(utf8_path(path))) {
        return true;
    }
    return SaveManager::write_planet_data(planet_dir, seed, dimension_id, nullptr);
}

bool read_existing_region(const std::string& file_path,
                          const std::string& expected_dimension,
                          int expected_rx, int expected_ry, int expected_rz,
                          std::vector<RegionChunkEntry>& entries) {
    entries.clear();
    const fs::path path = utf8_path(file_path);
    if (!fs::exists(path)) {
        return true;
    }

    std::string file_dimension_id;
    int file_rx = 0;
    int file_ry = 0;
    int file_rz = 0;
    if (!RegionFile::read(file_path, file_dimension_id,
                          file_rx, file_ry, file_rz, entries)) {
        return false;
    }
    return file_dimension_id == expected_dimension
        && file_rx == expected_rx
        && file_ry == expected_ry
        && file_rz == expected_rz;
}

} // namespace

std::vector<std::string> SaveManager::list_saves(
    const std::string& base_saves_dir) {
    std::vector<std::string> result;

    const fs::path saves_path = utf8_path(base_saves_dir);
    if (!fs::exists(saves_path) || !fs::is_directory(saves_path)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(saves_path)) {
        if (!entry.is_directory()) {
            continue;
        }

        std::string save_path = path_to_utf8(entry.path());
        std::string header_path = save_path + "/universe_header.bin";

        if (fs::exists(utf8_path(header_path)) &&
            fs::is_regular_file(utf8_path(header_path))) {
            result.push_back(path_to_utf8(entry.path().filename()));
        }
    }

    return result;
}

bool SaveManager::ensure_directory(const std::string& path) {
    const fs::path directory = utf8_path(path);
    if (fs::exists(directory)) {
        return fs::is_directory(directory);
    }

    std::error_code ec;
    bool created = fs::create_directories(directory, ec);
    return created && !ec;
}

// --- Per-dimension save / load ---

int SaveManager::save_dimension(const std::string& planet_dir,
                                int64_t seed,
                                const std::string& dimension_id,
                                const ChunkRegistry& world) {
    if (!ensure_directory(planet_dir)) {
        return -1;
    }

    std::string regions_dir = planet_dir + "/regions";
    if (!ensure_directory(regions_dir)) {
        return -1;
    }

    // Write planet_data.bin header (no summary during save_dimension).
    try {
        if (!write_planet_data(planet_dir, seed, dimension_id, nullptr)) {
            return -1;
        }
    } catch (...) {
        throw std::runtime_error(
            "unknown exception while writing planet_data.bin");
    }

    // Group chunks for this dimension by region.
    std::map<std::string, std::vector<RegionChunkEntry>> region_chunks;
    std::map<std::string, std::tuple<int, int, int, std::string>> region_meta;

    try {
        for (const auto& key : world.all_chunk_keys()) {
            if (key.dimension_id != dimension_id) {
                continue;
            }

            const ChunkData* chunk = world.get_chunk(
                key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
            if (chunk == nullptr) {
                continue;
            }

            int rx = RegionFile::to_region(key.chunk_x);
            int ry = RegionFile::to_region(key.chunk_y);
            int rz = RegionFile::to_region(key.chunk_z);

            std::string file_name = RegionFile::region_file_name(
                key.dimension_id, rx, ry, rz);

            std::vector<uint8_t> data;
            try {
                data = ChunkSerializer::serialize(key.dimension_id, *chunk);
            } catch (const std::exception& error) {
                std::ostringstream message;
                message << "chunk serialization failed at " << key.dimension_id
                        << " (" << key.chunk_x << "," << key.chunk_y << ","
                        << key.chunk_z << "): " << error.what();
                throw std::runtime_error(message.str());
            } catch (...) {
                std::ostringstream message;
                message << "chunk serialization raised an unknown exception at "
                        << key.dimension_id << " (" << key.chunk_x << ","
                        << key.chunk_y << "," << key.chunk_z << ")";
                throw std::runtime_error(message.str());
            }

            RegionChunkEntry entry;
            entry.local_x = static_cast<uint8_t>(RegionFile::to_local(key.chunk_x));
            entry.local_y = static_cast<uint8_t>(RegionFile::to_local(key.chunk_y));
            entry.local_z = static_cast<uint8_t>(RegionFile::to_local(key.chunk_z));
            entry.data = std::move(data);

            region_chunks[file_name].push_back(std::move(entry));
            region_meta[file_name] = std::make_tuple(rx, ry, rz, key.dimension_id);
        }
    } catch (const std::exception&) {
        throw;
    } catch (...) {
        throw std::runtime_error(
            "unknown exception while grouping chunks into regions");
    }

    // Write each region file.
    int saved_count = 0;
    try {
        for (auto& [file_name, chunks] : region_chunks) {
            auto& [rx, ry, rz, dim] = region_meta[file_name];
            std::string file_path = regions_dir + "/" + file_name;

            if (!RegionFile::write(file_path, rx, ry, rz, dim, chunks)) {
                return -1;
            }

            saved_count += static_cast<int>(chunks.size());
        }
    } catch (const std::exception&) {
        throw;
    } catch (...) {
        throw std::runtime_error(
            "unknown exception while writing region files");
    }

    return saved_count;
}

int SaveManager::load_dimension(const std::string& planet_dir,
                                const std::string& dimension_id,
                                ChunkRegistry& world,
                                int* legacy_skipped) {
    if (legacy_skipped != nullptr) {
        *legacy_skipped = 0;
    }

    // Read planet_data.bin to validate.
    int64_t seed = 0;
    std::string dim_id;
    PlanetSummaryData summary;
    bool has_summary = false;
    if (!read_planet_data(planet_dir, seed, dim_id, summary, has_summary)) {
        return -1;
    }

    std::string regions_dir = planet_dir + "/regions";
    const fs::path regions_path = utf8_path(regions_dir);
    if (!fs::exists(regions_path) || !fs::is_directory(regions_path)) {
        return 0;
    }

    // Load each region file in this planet's directory.
    int loaded_count = 0;
    for (const auto& entry : fs::directory_iterator(regions_path)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string file_path = path_to_utf8(entry.path());

        std::string file_dimension_id;
        int region_x, region_y, region_z;
        std::vector<RegionChunkEntry> entries;

        if (!RegionFile::read(file_path, file_dimension_id,
                              region_x, region_y, region_z, entries)) {
            // Detect legacy (pre-v2) region files and count them so the
            // caller can emit a one-time migration warning. Old 2D layer
            // saves used version 1 with a "{layer}~{rx}~{ry}.region" name
            // and 2D TerrainData; the reader was removed during the 2D→3D
            // refactor. We skip them gracefully instead of failing.
            uint8_t ver = RegionFile::peek_version(file_path);
            if (ver != 0 && ver != RegionFile::kVersion) {
                if (legacy_skipped != nullptr) {
                    ++(*legacy_skipped);
                }
            }
            continue;
        }

        if (file_dimension_id != dimension_id) {
            continue;
        }

        for (auto& chunk_entry : entries) {
            int chunk_x = region_x * RegionFile::kRegionSize
                        + static_cast<int>(chunk_entry.local_x);
            int chunk_y = region_y * RegionFile::kRegionSize
                        + static_cast<int>(chunk_entry.local_y);
            int chunk_z = region_z * RegionFile::kRegionSize
                        + static_cast<int>(chunk_entry.local_z);

            std::string unused_dimension_id;
            ChunkData chunk;
            if (!ChunkSerializer::deserialize(chunk_entry.data,
                                              unused_dimension_id, chunk)) {
                continue;
            }

            world.set_chunk(dimension_id, chunk_x, chunk_y, chunk_z,
                            std::move(chunk));
            loaded_count++;
        }
    }

    return loaded_count;
}

// --- Per-chunk save / load ---

bool SaveManager::save_chunk(const std::string& planet_dir,
                             int64_t seed,
                             const std::string& dimension_id,
                             const ChunkRegistry& world,
                             int chunk_x, int chunk_y, int chunk_z) {
    if (!ensure_directory(planet_dir)) {
        return false;
    }
    const std::string regions_dir = planet_dir + "/regions";
    if (!ensure_directory(regions_dir)) {
        return false;
    }
    if (!ensure_planet_header_if_missing(planet_dir, seed, dimension_id)) {
        return false;
    }

    const ChunkData* chunk = world.get_chunk(dimension_id, chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return false;
    }

    const int rx = RegionFile::to_region(chunk_x);
    const int ry = RegionFile::to_region(chunk_y);
    const int rz = RegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(RegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(RegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(RegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);

    std::vector<RegionChunkEntry> entries;
    if (!read_existing_region(file_path, dimension_id, rx, ry, rz, entries)) {
        return false;
    }

    RegionChunkEntry replacement;
    replacement.local_x = lx;
    replacement.local_y = ly;
    replacement.local_z = lz;
    replacement.data = ChunkSerializer::serialize(dimension_id, *chunk);

    bool replaced = false;
    for (auto& entry : entries) {
        if (entry.local_x == lx && entry.local_y == ly && entry.local_z == lz) {
            entry = std::move(replacement);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        entries.push_back(std::move(replacement));
    }

    return RegionFile::write(file_path, rx, ry, rz, dimension_id, entries);
}

bool SaveManager::load_chunk(const std::string& planet_dir,
                             const std::string& dimension_id,
                             ChunkRegistry& world,
                             int chunk_x, int chunk_y, int chunk_z) {
    const int rx = RegionFile::to_region(chunk_x);
    const int ry = RegionFile::to_region(chunk_y);
    const int rz = RegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(RegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(RegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(RegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);

    std::vector<RegionChunkEntry> entries;
    if (!read_existing_region(file_path, dimension_id, rx, ry, rz, entries)) {
        return false;
    }

    for (const auto& entry : entries) {
        if (entry.local_x != lx || entry.local_y != ly || entry.local_z != lz) {
            continue;
        }
        std::string unused_dimension_id;
        ChunkData chunk;
        if (!ChunkSerializer::deserialize(entry.data, unused_dimension_id, chunk)) {
            return false;
        }
        world.set_chunk(dimension_id, chunk_x, chunk_y, chunk_z, std::move(chunk));
        return true;
    }

    return false;
}

bool SaveManager::delete_chunk(const std::string& planet_dir,
                               const std::string& dimension_id,
                               int chunk_x, int chunk_y, int chunk_z) {
    const int rx = RegionFile::to_region(chunk_x);
    const int ry = RegionFile::to_region(chunk_y);
    const int rz = RegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(RegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(RegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(RegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);

    const fs::path path = utf8_path(file_path);
    if (!fs::exists(path)) {
        return true;
    }

    std::vector<RegionChunkEntry> entries;
    if (!read_existing_region(file_path, dimension_id, rx, ry, rz, entries)) {
        return false;
    }

    const auto old_size = entries.size();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
            [lx, ly, lz](const RegionChunkEntry& entry) {
                return entry.local_x == lx && entry.local_y == ly && entry.local_z == lz;
            }),
        entries.end());

    if (entries.size() == old_size) {
        return true;
    }

    if (entries.empty()) {
        std::error_code ec;
        fs::remove(path, ec);
        return !ec;
    }

    return RegionFile::write(file_path, rx, ry, rz, dimension_id, entries);
}

// --- Planet data (header + summary) ---

void SaveManager::write_string(std::ofstream& file, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    file.write(reinterpret_cast<const char*>(&len), sizeof(len));
    if (len > 0) {
        file.write(str.data(), len);
    }
}

bool SaveManager::read_string(std::ifstream& file, std::string& out,
                               uint32_t max_len) {
    uint32_t len;
    file.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (!file.good() || len > max_len) {
        return false;
    }
    out.resize(len);
    if (len > 0) {
        file.read(out.data(), len);
    }
    return file.good();
}

bool SaveManager::write_planet_data(const std::string& planet_dir,
                                     int64_t seed,
                                     const std::string& dimension_id,
                                     const PlanetSummaryData* summary) {
    if (!ensure_directory(planet_dir)) {
        return false;
    }

    std::string path = planet_dir + "/planet_data.bin";
    std::ofstream file(utf8_path(path), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Header section.
    uint8_t version = kPlanetDataVersion;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&seed), sizeof(seed));
    write_string(file, dimension_id);

    // Summary section.
    uint8_t has_summary = (summary != nullptr) ? 1 : 0;
    file.write(reinterpret_cast<const char*>(&has_summary), sizeof(has_summary));

    if (summary != nullptr) {
        file.write(reinterpret_cast<const char*>(&summary->captured_tick),
                   sizeof(summary->captured_tick));

        // Production lines.
        uint32_t pl_count = static_cast<uint32_t>(summary->production_lines.size());
        file.write(reinterpret_cast<const char*>(&pl_count), sizeof(pl_count));
        for (const auto& line : summary->production_lines) {
            write_string(file, line.recipe_key);
            file.write(reinterpret_cast<const char*>(&line.rate_per_minute),
                       sizeof(line.rate_per_minute));
            file.write(reinterpret_cast<const char*>(&line.active_count),
                       sizeof(line.active_count));
        }

        // Mining sites.
        uint32_t ms_count = static_cast<uint32_t>(summary->mining_sites.size());
        file.write(reinterpret_cast<const char*>(&ms_count), sizeof(ms_count));
        for (const auto& site : summary->mining_sites) {
            write_string(file, site.ore_key);
            file.write(reinterpret_cast<const char*>(&site.rate_per_minute),
                       sizeof(site.rate_per_minute));
            file.write(reinterpret_cast<const char*>(&site.remaining_approx),
                       sizeof(site.remaining_approx));
        }

        // Storage levels.
        uint32_t sl_count = static_cast<uint32_t>(summary->storage_levels.size());
        file.write(reinterpret_cast<const char*>(&sl_count), sizeof(sl_count));
        for (const auto& entry : summary->storage_levels) {
            write_string(file, entry.item_key);
            file.write(reinterpret_cast<const char*>(&entry.count),
                       sizeof(entry.count));
            file.write(reinterpret_cast<const char*>(&entry.capacity),
                       sizeof(entry.capacity));
        }

        // Power summary.
        file.write(reinterpret_cast<const char*>(&summary->power_consumption_mw),
                   sizeof(summary->power_consumption_mw));
        file.write(reinterpret_cast<const char*>(&summary->power_generation_mw),
                   sizeof(summary->power_generation_mw));
        file.write(reinterpret_cast<const char*>(&summary->power_surplus_mw),
                   sizeof(summary->power_surplus_mw));

        // Accumulated production.
        uint32_t ap_count = static_cast<uint32_t>(summary->accumulated_production.size());
        file.write(reinterpret_cast<const char*>(&ap_count), sizeof(ap_count));
        for (const auto& entry : summary->accumulated_production) {
            write_string(file, entry.item_key);
            file.write(reinterpret_cast<const char*>(&entry.amount),
                       sizeof(entry.amount));
        }

        // Accumulated consumption.
        uint32_t ac_count = static_cast<uint32_t>(summary->accumulated_consumption.size());
        file.write(reinterpret_cast<const char*>(&ac_count), sizeof(ac_count));
        for (const auto& entry : summary->accumulated_consumption) {
            write_string(file, entry.item_key);
            file.write(reinterpret_cast<const char*>(&entry.amount),
                       sizeof(entry.amount));
        }
    }

    file.close();
    return file.good();
}

bool SaveManager::read_planet_data(const std::string& planet_dir,
                                    int64_t& out_seed,
                                    std::string& out_dimension_id,
                                    PlanetSummaryData& out_summary,
                                    bool& out_has_summary) {
    std::string path = planet_dir + "/planet_data.bin";

    std::ifstream file(utf8_path(path), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    uint8_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good()) {
        return false;
    }

    if (version != kPlanetDataVersion) {
        return false;
    }

    // Read seed.
    file.read(reinterpret_cast<char*>(&out_seed), sizeof(out_seed));
    if (!file.good()) {
        return false;
    }

    // Read dimension_id.
    if (!read_string(file, out_dimension_id)) {
        return false;
    }

    // Read summary section.
    out_has_summary = false;
    uint8_t has_summary;
    file.read(reinterpret_cast<char*>(&has_summary), sizeof(has_summary));
    if (!file.good()) {
        return true;  // Header is valid, just no summary.
    }

    out_has_summary = (has_summary == 1);
    if (out_has_summary) {
        file.read(reinterpret_cast<char*>(&out_summary.captured_tick),
                  sizeof(out_summary.captured_tick));
        if (!file.good()) return false;

        // Production lines.
        uint32_t pl_count;
        file.read(reinterpret_cast<char*>(&pl_count), sizeof(pl_count));
        if (!file.good()) return false;
        out_summary.production_lines.resize(pl_count);
        for (uint32_t i = 0; i < pl_count; ++i) {
            if (!read_string(file, out_summary.production_lines[i].recipe_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.production_lines[i].rate_per_minute),
                      sizeof(float));
            if (!file.good()) return false;
            file.read(reinterpret_cast<char*>(&out_summary.production_lines[i].active_count),
                      sizeof(int32_t));
            if (!file.good()) return false;
        }

        // Mining sites.
        uint32_t ms_count;
        file.read(reinterpret_cast<char*>(&ms_count), sizeof(ms_count));
        if (!file.good()) return false;
        out_summary.mining_sites.resize(ms_count);
        for (uint32_t i = 0; i < ms_count; ++i) {
            if (!read_string(file, out_summary.mining_sites[i].ore_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.mining_sites[i].rate_per_minute),
                      sizeof(float));
            if (!file.good()) return false;
            file.read(reinterpret_cast<char*>(&out_summary.mining_sites[i].remaining_approx),
                      sizeof(int64_t));
            if (!file.good()) return false;
        }

        // Storage levels.
        uint32_t sl_count;
        file.read(reinterpret_cast<char*>(&sl_count), sizeof(sl_count));
        if (!file.good()) return false;
        out_summary.storage_levels.resize(sl_count);
        for (uint32_t i = 0; i < sl_count; ++i) {
            if (!read_string(file, out_summary.storage_levels[i].item_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.storage_levels[i].count),
                      sizeof(int32_t));
            if (!file.good()) return false;
            file.read(reinterpret_cast<char*>(&out_summary.storage_levels[i].capacity),
                      sizeof(int32_t));
            if (!file.good()) return false;
        }

        // Power summary.
        file.read(reinterpret_cast<char*>(&out_summary.power_consumption_mw), sizeof(float));
        if (!file.good()) return false;
        file.read(reinterpret_cast<char*>(&out_summary.power_generation_mw), sizeof(float));
        if (!file.good()) return false;
        file.read(reinterpret_cast<char*>(&out_summary.power_surplus_mw), sizeof(float));
        if (!file.good()) return false;

        // Accumulated production.
        uint32_t ap_count;
        file.read(reinterpret_cast<char*>(&ap_count), sizeof(ap_count));
        if (!file.good()) return false;
        out_summary.accumulated_production.resize(ap_count);
        for (uint32_t i = 0; i < ap_count; ++i) {
            if (!read_string(file, out_summary.accumulated_production[i].item_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.accumulated_production[i].amount),
                      sizeof(double));
            if (!file.good()) return false;
        }

        // Accumulated consumption.
        uint32_t ac_count;
        file.read(reinterpret_cast<char*>(&ac_count), sizeof(ac_count));
        if (!file.good()) return false;
        out_summary.accumulated_consumption.resize(ac_count);
        for (uint32_t i = 0; i < ac_count; ++i) {
            if (!read_string(file, out_summary.accumulated_consumption[i].item_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.accumulated_consumption[i].amount),
                      sizeof(double));
            if (!file.good()) return false;
        }
    }

    return true;
}

bool SaveManager::write_universe_header(const std::string& save_dir,
                                        int64_t seed,
                                        const std::string& universe_mode) {
    if (!ensure_directory(save_dir)) {
        return false;
    }

    std::string header_path = save_dir + "/universe_header.bin";
    std::ofstream file(utf8_path(header_path), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    uint8_t version = kUniverseHeaderVersion;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&seed), sizeof(seed));
    write_string(file, universe_mode);

    file.close();
    return file.good();
}

std::tuple<bool, int64_t, std::string> SaveManager::read_universe_header(
        const std::string& save_dir) {
    std::string header_path = save_dir + "/universe_header.bin";

    std::ifstream file(utf8_path(header_path), std::ios::binary);
    if (!file.is_open()) {
        return {false, 0, ""};
    }

    uint8_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good() || version != kUniverseHeaderVersion) {
        return {false, 0, ""};
    }

    int64_t seed;
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    if (!file.good()) {
        return {false, 0, ""};
    }

    std::string universe_mode;
    if (!read_string(file, universe_mode)) {
        return {false, 0, ""};
    }

    return {true, seed, universe_mode};
}

// --- Utility ---

std::vector<std::string> SaveManager::list_planets(
        const std::string& save_dir) {
    std::vector<std::string> result;

    std::string planets_dir = save_dir + "/planets";
    const fs::path planets_path = utf8_path(planets_dir);
    if (!fs::exists(planets_path) || !fs::is_directory(planets_path)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(planets_path)) {
        if (!entry.is_directory()) {
            continue;
        }

        // Only accept planet_data.bin (v2 format).
        std::string data_path =
            path_to_utf8(entry.path()) + "/planet_data.bin";
        if (fs::exists(utf8_path(data_path)) &&
            fs::is_regular_file(utf8_path(data_path))) {
            result.push_back(path_to_utf8(entry.path().filename()));
        }
    }

    return result;
}

std::string SaveManager::planet_dir(const std::string& save_dir,
                                    const std::string& dimension_id) {
    return save_dir + "/planets/" + dimension_id;
}

} // namespace science_and_theology
