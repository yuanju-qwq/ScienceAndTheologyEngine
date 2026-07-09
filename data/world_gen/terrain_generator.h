#pragma once

#include <memory>
#include <string>

#include "../defs/chunk_data.h"
#include "../defs/world_seed.h"
#include "noise_generator.h"
#include "world_gen_config.h"

namespace snt::data {

// Generates chunk terrain data using a multi-pass pipeline.
// Each pass adds one layer of detail to the chunk.
//
// Pass order:
//   1. Base Terrain  - fundamental material layout (flat or spherical)
//   2. Biome         - biome-specific material adjustments
//   3. Ore           - resource vein placement
//   4. Structure     - large structural features
//   5. Object        - small interactable objects
//   6. Gameplay      - spawn points and future single-world structures.
//
// When a PlanetConfig is present for the dimension, pass_base_terrain
// generates a spherical world: only blocks within the planet radius
// (plus terrain noise) are solid; everything else is air.
//
// Thread safety: generate_chunk() is fully reentrant and safe for
// concurrent invocation. world_seed_ is read-only after construction;
// all other state is stack-local. NoiseGenerator instances are
// independently seeded per call.
//
// Usage:
//   TerrainGenerator gen(WorldSeed(12345));
//   ChunkData chunk = gen.generate_chunk("overworld", 0, 0, 0);
class TerrainGenerator {
public:
    enum class LandformZone : uint8_t {
        BASIN = 0,
        PLAINS = 1,
        HILLS = 2,
        PLATEAU = 3,
        MOUNTAINS = 4,
        RUGGED = 5,
        COUNT = 6,
    };

    explicit TerrainGenerator(
        WorldSeed seed,
        std::shared_ptr<const WorldGenConfigSnapshot> config = nullptr);

    // Generates a complete chunk for the given dimension and chunk coordinates.
    // Returns a fully populated ChunkData ready to be inserted into WorldData.
    ChunkData generate_chunk(const std::string& dimension_id,
                             int chunk_x, int chunk_y, int chunk_z);

    // Read-only terrain query for maps, diagnostics, and generation tests.
    // The input direction does not need to be normalized.
    LandformZone landform_zone_at_direction(
        const std::string& dimension_id,
        float dir_x, float dir_y, float dir_z) const;

private:
    struct LandformSample {
        LandformZone zone = LandformZone::PLAINS;
        float terrain_offset = 0.0f;
    };

    struct MaterialIds {
        TerrainMaterialId air = 0;
        TerrainMaterialId stone = 0;
        TerrainMaterialId dirt = 0;
        TerrainMaterialId sand = 0;
        TerrainMaterialId water = 0;
        TerrainMaterialId lava = 0;
        TerrainMaterialId wood = 0;
        TerrainMaterialId leaves = 0;
        TerrainMaterialId deepstone = 0;
        TerrainMaterialId core_barrier = 0;
        TerrainMaterialId snow = 0;
        TerrainMaterialId ice = 0;
    };

    // Pass 1: Fill the chunk with base terrain materials.
    // Dispatches to flat or spherical generation based on PlanetConfig.
    void pass_base_terrain(const std::string& dimension_id,
                           int chunk_x, int chunk_y, int chunk_z,
                           TerrainData& terrain);

    // Flat world base terrain (original infinite-plane model).
    void pass_base_terrain_flat(const std::string& dimension_id,
                                int chunk_x, int chunk_y, int chunk_z,
                                TerrainData& terrain,
                                const BaseTerrainRule& rule);

    // Spherical planet base terrain.
    void pass_base_terrain_planet(const std::string& dimension_id,
                                  int chunk_x, int chunk_y, int chunk_z,
                                  TerrainData& terrain,
                                  const PlanetConfig& planet);

    // Pass 1b: Apply rock layer regional variation.
    // Replaces standard crust stone with rock-layer-specific materials
    // based on noise-driven regional selection and depth range.
    void pass_rock_layer(const std::string& dimension_id,
                         int chunk_x, int chunk_y, int chunk_z,
                         TerrainData& terrain);

    // Pass 2: Apply biome overrides (temperature, humidity, etc.).
    void pass_biome(const std::string& dimension_id,
                    int chunk_x, int chunk_y, int chunk_z,
                    TerrainData& terrain);

    // Pass 3: Place GT-style multi-ore vein groups.
    // Each vein group contains primary, secondary, between, and sporadic ores.
    // Veins are placed by 2D noise in x/z, constrained by depth range.
    void pass_ore_vein_group(const std::string& dimension_id,
                             int chunk_x, int chunk_y, int chunk_z,
                             TerrainData& terrain);

    // Pass 4: Place trees and small surface voxel features.
    // Also creates BlockEntityPlacement entries in chunk.block_entities.
    void pass_surface_objects(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z,
                   TerrainData& terrain,
                   std::vector<BlockEntityPlacement>& block_entities);

    // Place wild crops on dirt surfaces for Tier 1 wild foraging.
    // Wild crops are terrain-only (no BlockEntity) 鈥?mined for seeds.
    void place_wild_crops(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z,
                   TerrainData& terrain);

    // Pass 5: Place gameplay elements.
    void pass_gameplay(const std::string& dimension_id,
                       int chunk_x, int chunk_y, int chunk_z,
                       ChunkData& chunk);

    // Sets a cell's material and derives its flags automatically.
    void set_cell(TerrainData& terrain, int x, int y, int z,
                  TerrainMaterial material);
    void set_cell_id(TerrainData& terrain, int x, int y, int z,
                     TerrainMaterialId material);

    uint32_t flags_for_material(TerrainMaterial material) const;
    uint32_t flags_for_material_id(TerrainMaterialId material) const;
    TerrainMaterialId cell_material_id(const TerrainCell& cell) const;

    // Checks if a cell is solid stone (candidate for ore placement).
    bool is_stone_cell(const TerrainCell& cell) const;
    bool is_material(const TerrainCell& cell, TerrainMaterialId material) const;
    bool is_walkable_ground_cell(const TerrainCell& cell) const;
    bool has_near_material(const TerrainData& terrain, int x, int y, int z,
                           TerrainMaterialId material, int radius) const;
    bool has_floor_support(const TerrainData& terrain, int x, int y, int z,
                           TerrainMaterialId support_material) const;
    MaterialIds materials() const;

    // Flat world: compute surface height at (x, z).
    int surface_height_at(const NoiseGenerator& elevation_noise,
                          int global_x, int global_z,
                          const BaseTerrainRule& rule) const;

    // Flat world: compute cave noise at (x, y, z).
    float cave_noise_at(const NoiseGenerator& cave_noise,
                        int global_x, int global_y, int global_z,
                        const BaseTerrainRule& rule) const;

    // Planet: sample a continuous landform profile and surface radius at a
    // direction from planet center. All non-ocean land maps to one of the six
    // landform zones, with smooth interpolation between neighboring zones.
    LandformSample sample_planet_landform(
                                const NoiseGenerator& elevation_noise,
                                const NoiseGenerator& detail_noise,
                                float dir_x, float dir_y, float dir_z,
                                const PlanetConfig& planet) const;

    float planet_surface_radius(const NoiseGenerator& elevation_noise,
                                const NoiseGenerator& detail_noise,
                                float dir_x, float dir_y, float dir_z,
                                const PlanetConfig& planet) const;

    WorldSeed world_seed_;
    std::shared_ptr<const WorldGenConfigSnapshot> config_;
};

} // namespace science_and_theology
