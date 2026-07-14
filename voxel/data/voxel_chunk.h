// Generic voxel chunk identity and terrain payload.
//
// Gameplay-owned chunk sidecars intentionally live outside this type. Runtime,
// rendering, collision, and headless voxel tests can therefore operate without
// depending on species, machines, ecology, or game configuration.

#pragma once

#include "voxel/data/terrain_data.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace snt::voxel {

enum class ChunkState : uint8_t {
    Unloaded = 0,
    Generating = 1,
    Generated = 2,
    Active = 3,
    Sleeping = 4,
};

struct ChunkKey {
    std::string dimension_id;
    int chunk_x = 0;
    int chunk_y = 0;
    int chunk_z = 0;

    ChunkKey() = default;

    ChunkKey(std::string dimension, int x, int y, int z)
        : dimension_id(std::move(dimension)),
          chunk_x(x),
          chunk_y(y),
          chunk_z(z) {}

    bool operator==(const ChunkKey& other) const {
        return chunk_x == other.chunk_x
            && chunk_y == other.chunk_y
            && chunk_z == other.chunk_z
            && dimension_id == other.dimension_id;
    }
};

struct VoxelChunk {
    static constexpr int kChunkSize = 32;

    int chunk_x = 0;
    int chunk_y = 0;
    int chunk_z = 0;
    ChunkState state = ChunkState::Unloaded;
    TerrainData terrain;
};

}  // namespace snt::voxel

template <>
struct std::hash<snt::voxel::ChunkKey> {
    size_t operator()(const snt::voxel::ChunkKey& key) const {
        size_t hash = std::hash<std::string>()(key.dimension_id);
        hash ^= std::hash<int>()(key.chunk_x) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>()(key.chunk_y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>()(key.chunk_z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};
