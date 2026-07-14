// Generic main-thread voxel chunk storage.
//
// The registry owns only VoxelChunk terrain payloads. A game session keeps
// semantic sidecars in its own registry keyed by the same ChunkKey.

#pragma once

#include "voxel/data/voxel_chunk.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace snt::voxel {

class ChunkRegistry {
public:
    ChunkRegistry() = default;
    ~ChunkRegistry() = default;

    ChunkRegistry(const ChunkRegistry&) = delete;
    ChunkRegistry& operator=(const ChunkRegistry&) = delete;
    ChunkRegistry(ChunkRegistry&&) = default;
    ChunkRegistry& operator=(ChunkRegistry&&) = default;

    bool has_chunk(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z) const {
        return chunks_.contains(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
    }

    VoxelChunk* get_chunk(const std::string& dimension_id,
                          int chunk_x, int chunk_y, int chunk_z) {
        const auto it = chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
        return it == chunks_.end() ? nullptr : &it->second;
    }

    const VoxelChunk* get_chunk(const std::string& dimension_id,
                                int chunk_x, int chunk_y, int chunk_z) const {
        const auto it = chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
        return it == chunks_.end() ? nullptr : &it->second;
    }

    void set_chunk(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z, VoxelChunk chunk) {
        chunk.chunk_x = chunk_x;
        chunk.chunk_y = chunk_y;
        chunk.chunk_z = chunk_z;
        chunks_[make_key(dimension_id, chunk_x, chunk_y, chunk_z)] = std::move(chunk);
    }

    void remove_chunk(const std::string& dimension_id,
                      int chunk_x, int chunk_y, int chunk_z) {
        chunks_.erase(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
    }

    std::vector<ChunkKey> all_chunk_keys() const {
        std::vector<ChunkKey> keys;
        keys.reserve(chunks_.size());
        for (const auto& [key, chunk] : chunks_) {
            (void)chunk;
            keys.push_back(key);
        }
        return keys;
    }

    void clear() { chunks_.clear(); }
    size_t chunk_count() const { return chunks_.size(); }

private:
    static ChunkKey make_key(const std::string& dimension_id,
                             int chunk_x, int chunk_y, int chunk_z) {
        return ChunkKey{dimension_id, chunk_x, chunk_y, chunk_z};
    }

    std::unordered_map<ChunkKey, VoxelChunk> chunks_;
};

}  // namespace snt::voxel
