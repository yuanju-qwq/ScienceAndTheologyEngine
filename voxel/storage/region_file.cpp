#include "voxel/storage/region_file.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace snt::voxel {
namespace {

namespace fs = std::filesystem;

fs::path utf8_path(const std::string& path) {
    return fs::u8path(path);
}

void write_uint8(std::ofstream& file, uint8_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void write_int32(std::ofstream& file, int32_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void write_uint32(std::ofstream& file, uint32_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void write_string(std::ofstream& file, const std::string& value) {
    write_uint32(file, static_cast<uint32_t>(value.size()));
    if (!value.empty()) file.write(value.data(), static_cast<std::streamsize>(value.size()));
}

bool read_uint8(std::ifstream& file, uint8_t& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return file.good();
}

bool read_int32(std::ifstream& file, int32_t& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return file.good();
}

bool read_uint32(std::ifstream& file, uint32_t& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return file.good();
}

bool read_string(std::ifstream& file, std::string& value) {
    uint32_t length = 0;
    if (!read_uint32(file, length)) return false;
    value.resize(length);
    if (length > 0) file.read(value.data(), static_cast<std::streamsize>(length));
    return file.good();
}

}  // namespace

bool VoxelRegionFile::write(const std::string& file_path,
                            int region_x, int region_y, int region_z,
                            const std::string& dimension_id,
                            const std::vector<VoxelRegionEntry>& entries) {
    if (entries.size() > static_cast<size_t>(kRegionSize * kRegionSize * kRegionSize)) {
        return false;
    }

    std::ofstream file(utf8_path(file_path), std::ios::binary);
    if (!file.is_open()) return false;

    write_uint8(file, kCurrentVersion);
    write_int32(file, region_x);
    write_int32(file, region_y);
    write_int32(file, region_z);
    write_string(file, dimension_id);
    write_uint32(file, static_cast<uint32_t>(entries.size()));

    for (const auto& entry : entries) {
        if (entry.local_x >= kRegionSize || entry.local_y >= kRegionSize ||
            entry.local_z >= kRegionSize) {
            return false;
        }
        write_uint8(file, entry.local_x);
        write_uint8(file, entry.local_y);
        write_uint8(file, entry.local_z);
        write_uint32(file, static_cast<uint32_t>(entry.payload.size()));
    }

    for (const auto& entry : entries) {
        if (!entry.payload.empty()) {
            file.write(reinterpret_cast<const char*>(entry.payload.data()),
                       static_cast<std::streamsize>(entry.payload.size()));
        }
    }

    file.close();
    return file.good();
}

bool VoxelRegionFile::read(const std::string& file_path,
                           std::string& out_dimension_id,
                           int& out_region_x, int& out_region_y, int& out_region_z,
                           std::vector<VoxelRegionEntry>& out_entries) {
    std::ifstream file(utf8_path(file_path), std::ios::binary);
    if (!file.is_open()) return false;

    uint8_t version = 0;
    if (!read_uint8(file, version) || version != kCurrentVersion) return false;

    int32_t region_x = 0;
    int32_t region_y = 0;
    int32_t region_z = 0;
    if (!read_int32(file, region_x) || !read_int32(file, region_y) ||
        !read_int32(file, region_z) || !read_string(file, out_dimension_id)) {
        return false;
    }
    out_region_x = region_x;
    out_region_y = region_y;
    out_region_z = region_z;

    uint32_t count = 0;
    if (!read_uint32(file, count) ||
        count > static_cast<uint32_t>(kRegionSize * kRegionSize * kRegionSize)) {
        return false;
    }

    struct IndexEntry {
        uint8_t local_x = 0;
        uint8_t local_y = 0;
        uint8_t local_z = 0;
        uint32_t payload_size = 0;
    };
    std::vector<IndexEntry> index;
    index.reserve(count);
    for (uint32_t entry_index = 0; entry_index < count; ++entry_index) {
        IndexEntry entry;
        if (!read_uint8(file, entry.local_x) || !read_uint8(file, entry.local_y) ||
            !read_uint8(file, entry.local_z) || !read_uint32(file, entry.payload_size) ||
            entry.local_x >= kRegionSize || entry.local_y >= kRegionSize ||
            entry.local_z >= kRegionSize) {
            return false;
        }
        index.push_back(entry);
    }

    out_entries.clear();
    out_entries.reserve(count);
    for (const auto& index_entry : index) {
        VoxelRegionEntry entry;
        entry.local_x = index_entry.local_x;
        entry.local_y = index_entry.local_y;
        entry.local_z = index_entry.local_z;
        entry.payload.resize(index_entry.payload_size);
        if (!entry.payload.empty()) {
            file.read(reinterpret_cast<char*>(entry.payload.data()),
                      static_cast<std::streamsize>(entry.payload.size()));
        }
        if (!file.good()) return false;
        out_entries.push_back(std::move(entry));
    }

    return true;
}

std::string VoxelRegionFile::region_file_name(const std::string& dimension_id,
                                               int region_x, int region_y, int region_z) {
    std::ostringstream stream;
    stream << dimension_id << "~" << region_x << "~" << region_y
           << "~" << region_z << ".region";
    return stream.str();
}

}  // namespace snt::voxel
