// ChunkRegistry — chunk storage and spatial index for the voxel world.
//
// P2 task 4: extracted from WorldData (radical refactor).
// Namespace: snt::data.
//
// This is a pure data container responsible only for chunk storage and lookup.
// It does NOT own gameplay config, tick state, worldgen config, physics events,
// block entity registry, or machine collision overlay — those are managed by
// ECS components or dedicated systems (see ecs/components/world_components.h).
//
// Thread safety: main thread only. Not safe for concurrent access.
// The registry is accessed by simulation systems and the save layer.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "defs/chunk_data.h"

namespace snt::data {

// ChunkRegistry — chunk storage and spatial index.
//
// Replaces the chunk-storage portion of the legacy WorldData god-object.
// SaveManager, TerrainGenerator, and simulation systems reference this
// registry for chunk read/write. Block entities, machine collision, and
// mobile structures live in their own registries and are composed by the
// ECS World rather than nested here.
class ChunkRegistry {
public:
    ChunkRegistry() = default;
    ~ChunkRegistry() = default;

    // Disallow copy, allow move.
    ChunkRegistry(const ChunkRegistry&) = delete;
    ChunkRegistry& operator=(const ChunkRegistry&) = delete;
    ChunkRegistry(ChunkRegistry&&) = default;
    ChunkRegistry& operator=(ChunkRegistry&&) = default;

    // --- Chunk access ---

    bool has_chunk(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z) const;

    ChunkData* get_chunk(const std::string& dimension_id,
                         int chunk_x, int chunk_y, int chunk_z);

    const ChunkData* get_chunk(
        const std::string& dimension_id,
        int chunk_x, int chunk_y, int chunk_z) const;

    // Sets or replaces a chunk. Takes ownership.
    void set_chunk(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z, ChunkData chunk);

    // Removes a chunk. Does nothing if the chunk does not exist.
    void remove_chunk(const std::string& dimension_id,
                      int chunk_x, int chunk_y, int chunk_z);

    // Returns all chunk keys for iterating over the world.
    std::vector<ChunkKey> all_chunk_keys() const;

    // Removes all chunks.
    void clear();

    // Returns the total number of loaded chunks.
    size_t chunk_count() const { return chunks_.size(); }

private:
    static ChunkKey make_key(const std::string& dimension_id,
                             int chunk_x, int chunk_y, int chunk_z);

    // Chunk storage keyed by (dimension_id, chunk_x, chunk_y, chunk_z).
    std::unordered_map<ChunkKey, ChunkData> chunks_;
};

// --- Inline implementations ---

inline ChunkKey ChunkRegistry::make_key(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    return ChunkKey{dimension_id, chunk_x, chunk_y, chunk_z};
}

inline bool ChunkRegistry::has_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    return chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z)) != chunks_.end();
}

inline ChunkData* ChunkRegistry::get_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    auto it = chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

inline const ChunkData* ChunkRegistry::get_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    auto it = chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

inline void ChunkRegistry::set_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    ChunkData chunk) {
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunk.chunk_z = chunk_z;
    chunks_[make_key(dimension_id, chunk_x, chunk_y, chunk_z)] = std::move(chunk);
}

inline void ChunkRegistry::remove_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    chunks_.erase(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
}

inline std::vector<ChunkKey> ChunkRegistry::all_chunk_keys() const {
    std::vector<ChunkKey> keys;
    keys.reserve(chunks_.size());
    for (const auto& pair : chunks_) {
        keys.push_back(pair.first);
    }
    return keys;
}

inline void ChunkRegistry::clear() {
    chunks_.clear();
}

} // namespace snt::data
