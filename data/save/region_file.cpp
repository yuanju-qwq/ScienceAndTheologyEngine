#include "region_file.h"

#include <fstream>
#include <filesystem>
#include <sstream>

namespace snt::data {

namespace fs = std::filesystem;

static fs::path utf8_path(const std::string& path) {
    return fs::u8path(path);
}

static void write_uint8(std::ofstream& file, uint8_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

static void write_int32(std::ofstream& file, int32_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

static void write_uint32(std::ofstream& file, uint32_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

static void write_string(std::ofstream& file, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    write_uint32(file, len);
    if (len > 0) {
        file.write(str.data(), len);
    }
}

static bool read_uint8(std::ifstream& file, uint8_t& out) {
    file.read(reinterpret_cast<char*>(&out), sizeof(out));
    return file.good();
}

static bool read_int32(std::ifstream& file, int32_t& out) {
    file.read(reinterpret_cast<char*>(&out), sizeof(out));
    return file.good();
}

static bool read_uint32(std::ifstream& file, uint32_t& out) {
    file.read(reinterpret_cast<char*>(&out), sizeof(out));
    return file.good();
}

static bool read_string(std::ifstream& file, std::string& out) {
    uint32_t len;
    if (!read_uint32(file, len)) return false;
    if (len == 0) {
        out.clear();
        return true;
    }
    out.resize(len);
    file.read(&out[0], len);
    return file.good();
}

bool RegionFile::write(const std::string& file_path,
                       int region_x, int region_y, int region_z,
                       const std::string& dimension_id,
                       const std::vector<RegionChunkEntry>& chunks) {
    if (chunks.size() > static_cast<size_t>(kRegionSize * kRegionSize * kRegionSize))
        return false;

    std::ofstream file(utf8_path(file_path), std::ios::binary);
    if (!file.is_open()) return false;

    write_uint8(file, kVersion);
    write_int32(file, region_x);
    write_int32(file, region_y);
    write_int32(file, region_z);
    write_string(file, dimension_id);

    write_uint32(file, static_cast<uint32_t>(chunks.size()));

    for (const auto& chunk : chunks) {
        write_uint8(file, chunk.local_x);
        write_uint8(file, chunk.local_y);
        write_uint8(file, chunk.local_z);
        write_uint32(file, static_cast<uint32_t>(chunk.data.size()));
    }

    for (const auto& chunk : chunks) {
        file.write(reinterpret_cast<const char*>(chunk.data.data()),
                   chunk.data.size());
    }

    file.close();
    return file.good();
}

bool RegionFile::read(const std::string& file_path,
                      std::string& out_dimension_id,
                      int& out_region_x, int& out_region_y, int& out_region_z,
                      std::vector<RegionChunkEntry>& out_chunks) {
    std::ifstream file(utf8_path(file_path), std::ios::binary);
    if (!file.is_open()) return false;

    uint8_t version;
    if (!read_uint8(file, version)) return false;
    if (version != kVersion) return false;

    int32_t rx, ry;
    if (!read_int32(file, rx)) return false;
    if (!read_int32(file, ry)) return false;
    int32_t rz;
    if (!read_int32(file, rz)) return false;
    out_region_x = rx;
    out_region_y = ry;
    out_region_z = rz;

    if (!read_string(file, out_dimension_id)) return false;

    uint32_t count;
    if (!read_uint32(file, count)) return false;
    if (count > static_cast<uint32_t>(kRegionSize * kRegionSize * kRegionSize)) return false;

    struct IndexEntry {
        uint8_t local_x;
        uint8_t local_y;
        uint8_t local_z;
        uint32_t data_size;
    };
    std::vector<IndexEntry> index;
    index.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        IndexEntry entry;
        if (!read_uint8(file, entry.local_x)) return false;
        if (!read_uint8(file, entry.local_y)) return false;
        if (!read_uint8(file, entry.local_z)) return false;
        if (!read_uint32(file, entry.data_size)) return false;
        if (entry.local_x >= kRegionSize ||
            entry.local_y >= kRegionSize ||
            entry.local_z >= kRegionSize) {
            return false;
        }
        index.push_back(entry);
    }

    out_chunks.clear();
    out_chunks.reserve(count);

    for (const auto& entry : index) {
        RegionChunkEntry chunk_entry;
        chunk_entry.local_x = entry.local_x;
        chunk_entry.local_y = entry.local_y;
        chunk_entry.local_z = entry.local_z;
        chunk_entry.data.resize(entry.data_size);
        file.read(reinterpret_cast<char*>(chunk_entry.data.data()),
                  entry.data_size);
        if (!file.good()) return false;
        out_chunks.push_back(std::move(chunk_entry));
    }

    return true;
}

std::string RegionFile::region_file_name(const std::string& dimension_id,
                                         int region_x, int region_y, int region_z) {
    std::ostringstream oss;
    oss << dimension_id << "~" << region_x << "~" << region_y
        << "~" << region_z << ".region";
    return oss.str();
}

uint8_t RegionFile::peek_version(const std::string& file_path) {
    std::ifstream file(utf8_path(file_path), std::ios::binary);
    if (!file.is_open()) return 0;
    uint8_t version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good()) return 0;
    return version;
}

} // namespace science_and_theology
