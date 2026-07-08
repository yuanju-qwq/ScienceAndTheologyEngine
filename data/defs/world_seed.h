// WorldSeed — world generation seed and derived sub-seed derivation.
//
// Ported from src/core/world_gen/world_seed.hpp.
// Namespace: science_and_theology -> snt::data.
//
// All generators should derive their seeds from the world seed to ensure
// deterministic reproduction of the entire world from a single uint64_t.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace snt::data {

// Pass IDs for the generation pipeline.
enum class GenerationPass : uint32_t {
    BASE_TERRAIN   = 0,
    ROCK_LAYER     = 1,
    BIOME          = 2,
    ORE            = 3,
    STRUCTURE      = 4,
    OBJECT         = 5,
    GAMEPLAY       = 6,
    ORE_VEIN_GROUP = 7,
};

// Manages the world generation seed and derived parameters.
// All generators should derive their seeds from the world seed to ensure
// deterministic reproduction of the entire world from a single uint64_t.
class WorldSeed {
public:
    explicit WorldSeed(uint64_t seed = 0) : seed_(seed) {}

    uint64_t seed() const { return seed_; }

    // Derives a sub-seed for a specific generator pass.
    // Uses a simple mixing function so that different passes produce
    // independent but deterministic noise streams.
    uint64_t sub_seed(uint32_t pass_id) const {
        uint64_t h = seed_;
        h ^= static_cast<uint64_t>(pass_id) * 0x9e3779b97f4a7c15ULL;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }

    // Derives one stable noise-field seed for a dimension and generation
    // pass. Noise sampled with global coordinates must use this seed so the
    // permutation table remains identical on both sides of chunk borders.
    uint64_t dimension_seed(uint32_t pass_id,
                            const std::string& dimension_id) const {
        uint64_t h = sub_seed(pass_id);
        for (const unsigned char byte : dimension_id) {
            h ^= static_cast<uint64_t>(byte);
            h *= 0x100000001b3ULL;
        }
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        return h & 0x7FFFFFFFFFFFFFFFULL;
    }

    // Derives a sub-seed for a specific chunk and pass.
    uint64_t chunk_seed(uint32_t pass_id, int chunk_x, int chunk_y, int chunk_z) const {
        uint64_t h = sub_seed(pass_id);
        h ^= static_cast<uint64_t>(chunk_x) * 0x9e3779b97f4a7c15ULL;
        h ^= static_cast<uint64_t>(chunk_y) * 0x85ebca6b122509bbULL;
        h ^= static_cast<uint64_t>(chunk_z) * 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        // Mask to non-negative int64 range for GDScript interop.
        return h & 0x7FFFFFFFFFFFFFFFULL;
    }

    // Derives a deterministic connector ID from world seed and placement parameters.
    // Uses dimension_id, global voxel coordinates, and a local index to produce
    // a globally unique int64_t that is reproducible across sessions.
    // Hash is masked to non-negative range for safe GDScript interop.
    int64_t connector_id(const std::string& dimension_id,
                           int global_x, int global_y, int global_z, int index) const {
        uint64_t h = sub_seed(static_cast<uint32_t>(GenerationPass::GAMEPLAY));
        h ^= std::hash<std::string>{}(dimension_id) * 0x9e3779b97f4a7c15ULL;
        h ^= static_cast<uint64_t>(global_x) * 0x9e3779b97f4a7c15ULL;
        h ^= static_cast<uint64_t>(global_y) * 0x85ebca6b122509bbULL;
        h ^= static_cast<uint64_t>(global_z) * 0xd1b54a32d192ed03ULL;
        h ^= static_cast<uint64_t>(index) * 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        return static_cast<int64_t>(h & 0x7FFFFFFFFFFFFFFFULL);
    }

    std::string mechanism_id(const std::string& dimension_id,
                             int global_x, int global_y, int global_z, int index) const {
        uint64_t h = sub_seed(static_cast<uint32_t>(GenerationPass::GAMEPLAY) + 17);
        h ^= std::hash<std::string>{}(dimension_id) * 0x9e3779b97f4a7c15ULL;
        h ^= static_cast<uint64_t>(global_x) * 0x9e3779b97f4a7c15ULL;
        h ^= static_cast<uint64_t>(global_y) * 0x85ebca6b122509bbULL;
        h ^= static_cast<uint64_t>(global_z) * 0xd1b54a32d192ed03ULL;
        h ^= static_cast<uint64_t>(index) * 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        return "mechanism_" + std::to_string(h & 0x7FFFFFFFFFFFFFFFULL);
    }

    bool operator==(const WorldSeed& other) const {
        return seed_ == other.seed_;
    }

    bool operator!=(const WorldSeed& other) const {
        return seed_ != other.seed_;
    }

private:
    uint64_t seed_;
};

} // namespace snt::data
