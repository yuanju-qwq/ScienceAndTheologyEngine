#include "terrain_generator.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace snt::data {

namespace {

int global_coord(int chunk_coord, int local_coord) {
    return chunk_coord * ChunkData::kChunkSize + local_coord;
}

bool is_exposed_to_air(const TerrainData& terrain, int x, int y, int z,
                       TerrainMaterialId air) {
    if (y + 1 >= terrain.size_y) {
        return true;
    }
    return static_cast<TerrainMaterialId>(
        terrain.cell_at(x, y + 1, z).material) == air;
}

std::string normalize_dimension_id(const std::string& id) {
    if (id == "surface" || id.empty()) {
        return "overworld";
    }
    return id;
}

} // namespace

TerrainGenerator::TerrainGenerator(
    WorldSeed seed,
    std::shared_ptr<const WorldGenConfigSnapshot> config)
    : world_seed_(seed),
      config_(config ? std::move(config) : make_empty_world_gen_config()) {}

ChunkData TerrainGenerator::generate_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    const std::string normalized_dimension = normalize_dimension_id(dimension_id);
    ChunkData chunk;
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunk.chunk_z = chunk_z;
    chunk.state = ChunkState::GENERATED;
    chunk.terrain.resize(
        ChunkData::kChunkSize, ChunkData::kChunkSize, ChunkData::kChunkSize);

    pass_base_terrain(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk.terrain);
    pass_rock_layer(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk.terrain);
    pass_biome(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk.terrain);
    pass_ore_vein_group(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk.terrain);
    pass_surface_objects(normalized_dimension, chunk_x, chunk_y, chunk_z,
                         chunk.terrain, chunk.block_entities);
    pass_gameplay(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk);

    return chunk;
}

TerrainGenerator::LandformZone TerrainGenerator::landform_zone_at_direction(
    const std::string& dimension_id,
    float dir_x, float dir_y, float dir_z) const {
    const std::string normalized_dimension = normalize_dimension_id(dimension_id);
    const PlanetConfig* planet =
        config_->find_planet_config(normalized_dimension);
    const float length_sq = dir_x * dir_x + dir_y * dir_y + dir_z * dir_z;
    if (planet == nullptr || !planet->is_planet() || length_sq < 1.0e-12f) {
        return LandformZone::PLAINS;
    }

    const float inv_length = 1.0f / std::sqrt(length_sq);
    NoiseGenerator elevation_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN),
        normalized_dimension));
    NoiseGenerator detail_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 1,
        normalized_dimension));
    return sample_planet_landform(
        elevation_noise, detail_noise,
        dir_x * inv_length, dir_y * inv_length, dir_z * inv_length,
        *planet).zone;
}

// --- Pass 1: Base Terrain ---

void TerrainGenerator::pass_base_terrain(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain) {
    // Check if this dimension has a planet configuration.
    const PlanetConfig* planet = config_->find_planet_config(dimension_id);
    // Low-frequency diagnostic: only log for chunk (0,0,0) to verify
    // dimension_id matching and PlanetConfig lookup at spawn.
    if (chunk_x == 0 && chunk_y == 0 && chunk_z == 0) {
        printf("[TerrainGen] pass_base_terrain chunk(0,0,0) dim='%s' "
               "planet_found=%d is_planet=%d",
               dimension_id.c_str(),
               planet != nullptr ? 1 : 0,
               (planet && planet->is_planet()) ? 1 : 0);
        if (planet) {
            printf(" center=(%.1f,%.1f,%.1f) radius=%.1f sea_level=%.1f",
                   planet->center_x, planet->center_y, planet->center_z,
                   planet->planet_radius,
                   planet->planet_radius + planet->terrain_height_scale * planet->sea_level_fraction);
        }
        printf("\n");
        fflush(stdout);
    }
    if (planet && planet->is_planet()) {
        pass_base_terrain_planet(dimension_id, chunk_x, chunk_y, chunk_z,
                                 terrain, *planet);
        return;
    }

    // Fall back to flat world generation.
    const BaseTerrainRule* rule = config_->find_base_rule(dimension_id);
    if (rule == nullptr) {
        const auto mat = materials();
        for (int y = 0; y < terrain.size_y; ++y) {
            for (int z = 0; z < terrain.size_z; ++z) {
                for (int x = 0; x < terrain.size_x; ++x) {
                    set_cell_id(terrain, x, y, z, mat.air);
                }
            }
        }
        return;
    }

    pass_base_terrain_flat(dimension_id, chunk_x, chunk_y, chunk_z, terrain, *rule);
}

void TerrainGenerator::pass_base_terrain_flat(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain, const BaseTerrainRule& rule) {
    (void)dimension_id;
    const auto mat = materials();

    NoiseGenerator elevation_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN),
        dimension_id));
    NoiseGenerator detail_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 1,
        dimension_id));
    NoiseGenerator cave_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 2,
        dimension_id));

    for (int y = 0; y < terrain.size_y; ++y) {
        for (int z = 0; z < terrain.size_z; ++z) {
            for (int x = 0; x < terrain.size_x; ++x) {
                const int global_x = global_coord(chunk_x, x);
                const int global_y = global_coord(chunk_y, y);
                const int global_z = global_coord(chunk_z, z);
                const int surface_height =
                    surface_height_at(elevation_noise, global_x, global_z, rule);

                TerrainMaterialId material = mat.air;
                if (global_y > surface_height) {
                    if (global_y <= 0 && surface_height < 0) {
                        material = rule.low_elevation_material != 0
                            ? rule.low_elevation_material
                            : mat.water;
                    } else {
                        material = mat.air;
                    }
                } else if (global_y == surface_height) {
                    const float detail = detail_noise.noise_3d_scaled(
                        static_cast<float>(global_x + 10000),
                        static_cast<float>(global_y),
                        static_cast<float>(global_z + 10000),
                        rule.detail_scale, rule.detail_octaves);
                    if (surface_height >= 8 || std::abs(detail) > rule.stone_elevation_abs_min) {
                        material = rule.high_elevation_material != 0
                            ? rule.high_elevation_material
                            : mat.stone;
                    } else {
                        material = rule.default_material != 0
                            ? rule.default_material
                            : mat.dirt;
                    }
                } else if (global_y >= surface_height - 3 && global_y >= -8) {
                    material = rule.default_material != 0
                        ? rule.default_material
                        : mat.dirt;
                } else {
                    material = rule.high_elevation_material != 0
                        ? rule.high_elevation_material
                        : mat.stone;
                }

                if (material != mat.air && global_y < surface_height - 3) {
                    const float cave = cave_noise_at(
                        cave_noise, global_x, global_y, global_z, rule);
                    float threshold = rule.cave_threshold;
                    threshold += std::clamp((-global_y - 8) / 64.0f, 0.0f, 0.18f);
                    if (cave > threshold) {
                        material = global_y < -24 ? mat.lava : mat.air;
                    }
                }

                set_cell_id(terrain, x, y, z, material);
            }
        }
    }
}

void TerrainGenerator::pass_base_terrain_planet(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain, const PlanetConfig& planet) {
    const auto mat = materials();
    const TerrainMaterialId snow_material =
        mat.snow != 0 ? mat.snow : mat.dirt;
    const TerrainMaterialId ice_material =
        mat.ice != 0 ? mat.ice : mat.water;

    NoiseGenerator elevation_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN),
        dimension_id));
    NoiseGenerator detail_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 1,
        dimension_id));
    NoiseGenerator cave_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 2,
        dimension_id));
    NoiseGenerator core_boundary_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 3,
        dimension_id));

    // Radial layer boundaries.
    const float core_r = planet.planet_radius * planet.core_radius_ratio;
    const float mantle_r = planet.planet_radius * planet.mantle_radius_ratio;
    const float sea_level_radius =
        planet.planet_radius + planet.terrain_height_scale * planet.sea_level_fraction;

    for (int y = 0; y < terrain.size_y; ++y) {
        for (int z = 0; z < terrain.size_z; ++z) {
            for (int x = 0; x < terrain.size_x; ++x) {
                const float global_x = static_cast<float>(global_coord(chunk_x, x));
                const float global_y = static_cast<float>(global_coord(chunk_y, y));
                const float global_z = static_cast<float>(global_coord(chunk_z, z));

                // Vector from planet center to this block.
                const float dx = global_x - planet.center_x;
                const float dy = global_y - planet.center_y;
                const float dz = global_z - planet.center_z;
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                // Direction from center (normalized).
                const float inv_dist = (dist > 0.001f) ? (1.0f / dist) : 0.0f;
                const float dir_x = dx * inv_dist;
                const float dir_y = dy * inv_dist;
                const float dir_z = dz * inv_dist;

                // Compute a continuous, exhaustive landform profile at this
                // direction. Ocean is represented by a submerged basin.
                const LandformSample landform = sample_planet_landform(
                    elevation_noise, detail_noise,
                    dir_x, dir_y, dir_z, planet);
                float surface_r =
                    planet.planet_radius + landform.terrain_offset;

                // local_center places the north-pole spawn above (center_x,
                // center_z). Keep a small landing area there so a new player
                // never starts over an ocean or lava surface.
                constexpr float kSpawnFlatRadius = 32.0f;
                constexpr float kSpawnTransitionRadius = 64.0f;
                const float spawn_distance = std::sqrt(dx * dx + dz * dz);
                if (dir_y > 0.0f && spawn_distance < kSpawnTransitionRadius) {
                    float blend = std::clamp(
                        (kSpawnTransitionRadius - spawn_distance)
                            / (kSpawnTransitionRadius - kSpawnFlatRadius),
                        0.0f, 1.0f);
                    blend = blend * blend * (3.0f - 2.0f * blend);
                    // Convert a fixed north-pole Y level to a radius along
                    // this direction. This creates a genuinely horizontal
                    // landing plateau instead of merely filling low spots.
                    const float safe_surface_r =
                        (sea_level_radius + 2.0f) / std::max(dir_y, 0.001f);
                    surface_r += (safe_surface_r - surface_r) * blend;
                }

                // Breathable planets have permanent polar snow caps and a
                // one-block ice shell over polar oceans.
                const bool is_polar =
                    planet.atmosphere_type == ATMO_BREATHABLE
                    && std::abs(dir_y) >= 0.85f;

                // Compute perturbed core boundary at this direction.
                const float core_noise_val = core_boundary_noise.noise_3d_scaled(
                    dir_x, dir_y, dir_z,
                    planet.core_boundary_noise_scale,
                    planet.core_boundary_noise_octaves);
                const float core_boundary_offset = core_noise_val
                    * core_r * planet.core_boundary_noise_amplitude;
                const float actual_core_r = core_r + core_boundary_offset;

                TerrainMaterialId material = mat.air;
                if (dist <= actual_core_r) {
                    // Inner core: indestructible barrier.
                    material = mat.core_barrier;
                } else if (dist <= mantle_r) {
                    // Between core and mantle: outer core / lava zone.
                    // Only the outer portion is lava; the inner portion
                    // near the core barrier is also core_barrier.
                    const float outer_core_start = actual_core_r;
                    const float outer_core_end = mantle_r;
                    const float outer_core_thickness =
                        outer_core_end - outer_core_start;

                    if (outer_core_thickness > 0.0f) {
                        const float t = (dist - outer_core_start) / outer_core_thickness;
                        if (t < 0.3f) {
                            // Close to core barrier: also indestructible.
                            material = mat.core_barrier;
                        } else {
                            // Outer core: lava.
                            material = mat.lava;
                        }
                    } else {
                        // Mantle radius too close to core: treat as core barrier.
                        material = mat.core_barrier;
                    }
                } else if (dist <= surface_r) {
                    // Crust: inside the planet surface.
                    const float depth = surface_r - dist;
                    const float crust_thickness = surface_r - mantle_r;
                    const float depth_ratio = (crust_thickness > 0.0f)
                        ? (depth / crust_thickness) : 0.0f;

                    if (depth < 1.0f) {
                        // Surface layer.
                        if (is_polar) {
                            material = snow_material;
                        } else if (surface_r < sea_level_radius) {
                            material = mat.sand;
                        } else if (landform.zone == LandformZone::MOUNTAINS
                                   || landform.zone == LandformZone::RUGGED) {
                            material = mat.stone;
                        } else if (landform.zone == LandformZone::BASIN
                                   && planet.sea_level_fraction <= 0.01f) {
                            material = mat.sand;
                        } else {
                            material = mat.dirt;
                        }
                    } else if (depth < 4.0f) {
                        // Subsurface: dirt.
                        material = mat.dirt;
                    } else if (depth_ratio > 0.6f) {
                        // Deep crust near mantle: deepstone (very hard).
                        material = mat.deepstone;
                    } else {
                        // Standard crust: stone.
                        material = mat.stone;
                    }

                    // Cave generation in the crust (not in deepstone zone).
                    if (depth > 4.0f && depth_ratio <= 0.6f) {
                        const float cave = cave_noise.noise_3d_scaled(
                            global_x, global_y, global_z,
                            planet.cave_noise_scale, planet.cave_octaves);
                        float threshold = planet.cave_threshold;
                        // Deeper = slightly more caves.
                        threshold -= std::clamp(depth / 128.0f, 0.0f, 0.15f);
                        if (cave > threshold) {
                            material = mat.air;
                        }
                    }
                } else if (dist <= sea_level_radius) {
                    // Above surface but below sea level: water, capped with
                    // one block of ice in permanent polar regions.
                    material = is_polar && sea_level_radius - dist < 1.0f
                        ? ice_material
                        : mat.water;
                }

                set_cell_id(terrain, x, y, z, material);
            }
        }
    }
}

// --- Pass 1b: Rock Layer ---

void TerrainGenerator::pass_rock_layer(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain) {
    std::vector<const RockLayerRule*> rules;
    for (const auto& rule : config_->rock_layer_rules) {
        if (rule.dimension_id == dimension_id) {
            rules.push_back(&rule);
        }
    }
    if (rules.empty()) {
        return;
    }

    const auto mat = materials();
    const PlanetConfig* planet = config_->find_planet_config(dimension_id);

    NoiseGenerator rock_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::ROCK_LAYER),
        dimension_id));

    for (int y = 0; y < terrain.size_y; ++y) {
        for (int z = 0; z < terrain.size_z; ++z) {
            for (int x = 0; x < terrain.size_x; ++x) {
                TerrainCell& cell = terrain.cell_at(x, y, z);
                // Only replace solid mineable blocks (stone, deepstone).
                if (!cell.is_solid() || !cell.is_mineable()) {
                    continue;
                }

                const int global_x = global_coord(chunk_x, x);
                const int global_y = global_coord(chunk_y, y);
                const int global_z = global_coord(chunk_z, z);

                // Compute depth from surface for this block.
                float depth = 0.0f;
                if (planet && planet->is_planet()) {
                    // Spherical planet: depth = surface_radius - distance.
                    const float dx = static_cast<float>(global_x) - planet->center_x;
                    const float dy = static_cast<float>(global_y) - planet->center_y;
                    const float dz = static_cast<float>(global_z) - planet->center_z;
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    const float inv_dist = (dist > 0.001f) ? (1.0f / dist) : 0.0f;
                    NoiseGenerator elev_noise(world_seed_.dimension_seed(
                        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN),
                        dimension_id));
                    NoiseGenerator detail_noise(world_seed_.dimension_seed(
                        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 1,
                        dimension_id));
                    const float surface_r = planet_surface_radius(
                        elev_noise, detail_noise,
                        dx * inv_dist, dy * inv_dist, dz * inv_dist, *planet);
                    depth = surface_r - dist;
                } else {
                    // Flat world: depth = surface_height - global_y.
                    const BaseTerrainRule* base_rule =
                        config_->find_base_rule(dimension_id);
                    if (base_rule) {
                        NoiseGenerator elev_noise(world_seed_.dimension_seed(
                            static_cast<uint32_t>(GenerationPass::BASE_TERRAIN),
                            dimension_id));
                        const int surface_height = surface_height_at(
                            elev_noise, global_x, global_z, *base_rule);
                        depth = static_cast<float>(surface_height - global_y);
                    }
                }

                if (depth < 0.0f) {
                    continue;
                }

                // Compute rock layer noise for regional selection.
                // Use the surface direction for planet, or x/z for flat world.
                float noise_val = 0.0f;
                if (planet && planet->is_planet()) {
                    const float dx = static_cast<float>(global_x) - planet->center_x;
                    const float dy = static_cast<float>(global_y) - planet->center_y;
                    const float dz = static_cast<float>(global_z) - planet->center_z;
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    const float inv_dist = (dist > 0.001f) ? (1.0f / dist) : 0.0f;
                    // Use surface direction for rock layer noise so that
                    // the same region on the surface extends underground.
                    noise_val = rock_noise.noise_3d_scaled(
                        dx * inv_dist * planet->planet_radius,
                        dy * inv_dist * planet->planet_radius,
                        dz * inv_dist * planet->planet_radius,
                        rules.front()->noise_scale,
                        rules.front()->noise_octaves);
                } else {
                    noise_val = rock_noise.noise_3d_scaled(
                        static_cast<float>(global_x),
                        0.0f,
                        static_cast<float>(global_z),
                        rules.front()->noise_scale,
                        rules.front()->noise_octaves);
                }

                // Find the matching rock layer rule.
                for (const RockLayerRule* rule : rules) {
                    if (depth < rule->depth_min || depth > rule->depth_max) {
                        continue;
                    }
                    if (noise_val < rule->noise_min || noise_val > rule->noise_max) {
                        continue;
                    }
                    // This block is in this rock layer's region and depth range.
                    // Only replace if the current material matches the layer's
                    // rock material (or is a generic stone/deepstone).
                    const TerrainMaterialId cur = cell_material_id(cell);
                    if (cur == rule->rock_material ||
                        (rule->rock_material == mat.stone && cur == mat.stone) ||
                        (rule->rock_material == mat.deepstone && cur == mat.deepstone)) {
                        // Already the correct material for this layer.
                        // Update collapse_chance from the rock layer if the
                        // material definition has a different default.
                        // (Flags are already set by set_cell_id.)
                        break;
                    }
                    // Replace generic stone with the rock layer's material.
                    if ((cur == mat.stone || cur == mat.deepstone) &&
                        rule->rock_material != 0) {
                        set_cell_id(terrain, x, y, z, rule->rock_material);
                    }
                    break;
                }
            }
        }
    }
}

// --- Pass 2: Biome ---

void TerrainGenerator::pass_biome(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain) {
    std::vector<const BiomeRule*> rules;
    for (const auto& rule : config_->biome_rules) {
        if (rule.dimension_id == dimension_id) {
            rules.push_back(&rule);
        }
    }
    if (rules.empty()) {
        return;
    }

    const auto mat = materials();
    NoiseGenerator temp_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BIOME), dimension_id));
    NoiseGenerator humidity_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BIOME) + 1, dimension_id));

    for (int y = 0; y < terrain.size_y; ++y) {
        for (int z = 0; z < terrain.size_z; ++z) {
            for (int x = 0; x < terrain.size_x; ++x) {
                TerrainCell& cell = terrain.cell_at(x, y, z);
                if (!is_exposed_to_air(terrain, x, y, z, mat.air)) {
                    continue;
                }

                const int global_x = global_coord(chunk_x, x);
                const int global_y = global_coord(chunk_y, y);
                const int global_z = global_coord(chunk_z, z);
                const float temperature = temp_noise.noise_3d_scaled(
                    static_cast<float>(global_x),
                    static_cast<float>(global_y),
                    static_cast<float>(global_z),
                    0.015f, 3);
                const float humidity = humidity_noise.noise_3d_scaled(
                    static_cast<float>(global_x + 2000),
                    static_cast<float>(global_y + 2000),
                    static_cast<float>(global_z + 2000),
                    0.02f, 3);

                for (const BiomeRule* rule : rules) {
                    if (!is_material(cell, rule->source_material)) {
                        continue;
                    }
                    if (temperature < rule->temperature_min ||
                        temperature > rule->temperature_max ||
                        humidity < rule->humidity_min ||
                        humidity > rule->humidity_max) {
                        continue;
                    }
                    if (rule->requires_near_material &&
                        !has_near_material(
                            terrain, x, y, z, rule->near_material, rule->near_radius)) {
                        continue;
                    }
                    if (rule->requires_floor_support &&
                        !has_floor_support(terrain, x, y, z, rule->support_material)) {
                        continue;
                    }
                    if (rule->detail_threshold > -1.0f) {
                        const float detail = humidity_noise.noise_3d_scaled(
                            static_cast<float>(global_x + 4000),
                            static_cast<float>(global_y + 4000),
                            static_cast<float>(global_z + 4000),
                            rule->detail_scale,
                            rule->detail_octaves);
                        if (detail <= rule->detail_threshold) {
                            continue;
                        }
                    }
                    set_cell_id(terrain, x, y, z, rule->result_material);
                    break;
                }
            }
        }
    }
}

// --- Pass 3: Ore Vein Groups (GT-style) ---

void TerrainGenerator::pass_ore_vein_group(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain) {
    // Collect vein groups for this dimension.
    std::vector<const OreVeinGroup*> groups;
    for (const auto& group : config_->ore_vein_groups) {
        if (group.dimension_id == dimension_id) {
            groups.push_back(&group);
        }
    }
    if (groups.empty()) {
        return;
    }

    // Compute total weight for weighted selection.
    float total_weight = 0.0f;
    for (const auto* group : groups) {
        total_weight += group->weight;
    }
    if (total_weight <= 0.0f) {
        return;
    }

    const PlanetConfig* planet = config_->find_planet_config(dimension_id);

    // Noise generators for vein group placement.
    // vein_select_noise: selects which vein group occupies each position.
    // vein_shape_noise: determines the shape/density within the vein.
    // ore_type_noise: determines primary/secondary/between/sporadic distribution.
    NoiseGenerator vein_select_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::ORE_VEIN_GROUP),
        dimension_id));
    NoiseGenerator vein_shape_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::ORE_VEIN_GROUP) + 1,
        dimension_id));
    NoiseGenerator ore_type_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::ORE_VEIN_GROUP) + 2,
        dimension_id));

    // Reuse base terrain noise for depth computation.
    NoiseGenerator elev_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN),
        dimension_id));
    NoiseGenerator detail_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 1,
        dimension_id));

    for (int y = 0; y < terrain.size_y; ++y) {
        for (int z = 0; z < terrain.size_z; ++z) {
            for (int x = 0; x < terrain.size_x; ++x) {
                const int global_x = global_coord(chunk_x, x);
                const int global_y = global_coord(chunk_y, y);
                const int global_z = global_coord(chunk_z, z);

                // Compute depth from surface.
                float depth = 0.0f;
                if (planet && planet->is_planet()) {
                    const float dx = static_cast<float>(global_x) - planet->center_x;
                    const float dy = static_cast<float>(global_y) - planet->center_y;
                    const float dz = static_cast<float>(global_z) - planet->center_z;
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    const float inv_dist = (dist > 0.001f) ? (1.0f / dist) : 0.0f;
                    const float surface_r = planet_surface_radius(
                        elev_noise, detail_noise,
                        dx * inv_dist, dy * inv_dist, dz * inv_dist, *planet);
                    depth = surface_r - dist;
                } else {
                    const BaseTerrainRule* base_rule =
                        config_->find_base_rule(dimension_id);
                    if (base_rule) {
                        const int surface_height = surface_height_at(
                            elev_noise, global_x, global_z, *base_rule);
                        depth = static_cast<float>(surface_height - global_y);
                    }
                }

                // Use vein selection noise to pick a vein group.
                // The noise value is mapped to a weighted selection.
                const float select_val = vein_select_noise.noise_3d_scaled(
                    static_cast<float>(global_x),
                    static_cast<float>(global_y),
                    static_cast<float>(global_z),
                    0.04f, 2);

                // Weighted selection: map select_val from [-1,1] to [0,total_weight].
                const float weighted_pos = (select_val + 1.0f) * 0.5f * total_weight;
                float accumulated = 0.0f;
                const OreVeinGroup* selected_group = nullptr;
                for (const auto* group : groups) {
                    accumulated += group->weight;
                    if (weighted_pos <= accumulated) {
                        selected_group = group;
                        break;
                    }
                }
                if (selected_group == nullptr) {
                    selected_group = groups.back();
                }

                // Check depth range for this vein group.
                if (depth < selected_group->depth_min || depth > selected_group->depth_max) {
                    continue;
                }

                // Check that the current cell is the host material.
                TerrainCell& cell = terrain.cell_at(x, y, z);
                if (!is_material(cell, selected_group->host_material)) {
                    continue;
                }

                // Vein shape: determines if this block is within the vein.
                // Scale is inversely proportional to radius (larger veins = wider shape).
                const float shape_scale = 1.0f / std::max(1.0f, selected_group->radius);
                const float shape_val = vein_shape_noise.noise_3d_scaled(
                    static_cast<float>(global_x),
                    static_cast<float>(global_y),
                    static_cast<float>(global_z),
                    shape_scale, 3);

                // The vein exists where shape_val > (1.0 - density).
                // Higher density = more of the vein region is filled.
                const float shape_threshold = 1.0f - selected_group->density;
                if (shape_val <= shape_threshold) {
                    continue;
                }

                // Determine ore type based on ore_type_noise.
                // Distribution: primary ~40%, secondary ~30%, between ~20%, sporadic ~10%.
                const float ore_val = ore_type_noise.noise_3d_scaled(
                    static_cast<float>(global_x + 10000),
                    static_cast<float>(global_y + 10000),
                    static_cast<float>(global_z + 10000),
                    0.1f, 2);
                // Map ore_val from [-1,1] to [0,1].
                const float ore_normalized = (ore_val + 1.0f) * 0.5f;

                TerrainMaterialId ore_to_place = 0;
                if (ore_normalized < 0.4f && selected_group->primary_ore != 0) {
                    ore_to_place = selected_group->primary_ore;
                } else if (ore_normalized < 0.7f && selected_group->secondary_ore != 0) {
                    ore_to_place = selected_group->secondary_ore;
                } else if (ore_normalized < 0.9f && selected_group->between_ore != 0) {
                    ore_to_place = selected_group->between_ore;
                } else if (selected_group->sporadic_ore != 0) {
                    ore_to_place = selected_group->sporadic_ore;
                }

                if (ore_to_place != 0) {
                    set_cell_id(terrain, x, y, z, ore_to_place);
                }
            }
        }
    }
}

// --- Pass 4: Surface Objects ---

namespace {

// Place a sphere-shaped canopy (oak, maple, cherry, olive).
void place_canopy_sphere(
    TerrainData& terrain, int cx, int cy, int cz,
    int radius, TerrainMaterialId leaves_mat) {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) + std::abs(dz) + std::abs(dy) > radius + 1) {
                    continue;
                }
                const int nx = cx + dx;
                const int ny = cy + dy;
                const int nz = cz + dz;
                if (!terrain.is_valid_cell(nx, ny, nz)) continue;
                if (terrain.cell_at(nx, ny, nz).material != 0) continue;
                terrain.cell_at(nx, ny, nz).material =
                    static_cast<TerrainMaterial>(leaves_mat);
                terrain.cell_at(nx, ny, nz).flags = 0;
            }
        }
    }
}

// Place a cone-shaped canopy (spruce, sequoia).
// Layers get progressively narrower toward the top.
void place_canopy_cone(
    TerrainData& terrain, int cx, int base_cy, int cz,
    int radius, int height, TerrainMaterialId leaves_mat) {
    for (int layer = 0; layer < height; ++layer) {
        const int cy = base_cy + layer;
        const int layer_radius = std::max(0, radius - layer);
        for (int dz = -layer_radius; dz <= layer_radius; ++dz) {
            for (int dx = -layer_radius; dx <= layer_radius; ++dx) {
                if (std::abs(dx) + std::abs(dz) > layer_radius) continue;
                const int nx = cx + dx;
                const int ny = cy;
                const int nz = cz + dz;
                if (!terrain.is_valid_cell(nx, ny, nz)) continue;
                if (terrain.cell_at(nx, ny, nz).material != 0) continue;
                terrain.cell_at(nx, ny, nz).material =
                    static_cast<TerrainMaterial>(leaves_mat);
                terrain.cell_at(nx, ny, nz).flags = 0;
            }
        }
    }
}

// Place an umbrella-shaped canopy (acacia).
// Wide flat top on a short trunk section.
void place_canopy_umbrella(
    TerrainData& terrain, int cx, int cy, int cz,
    int radius, TerrainMaterialId leaves_mat) {
    for (int dy = 0; dy <= 1; ++dy) {
        const int r = (dy == 0) ? radius : radius - 1;
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) + std::abs(dz) > r + 1) continue;
                const int nx = cx + dx;
                const int ny = cy + dy;
                const int nz = cz + dz;
                if (!terrain.is_valid_cell(nx, ny, nz)) continue;
                if (terrain.cell_at(nx, ny, nz).material != 0) continue;
                terrain.cell_at(nx, ny, nz).material =
                    static_cast<TerrainMaterial>(leaves_mat);
                terrain.cell_at(nx, ny, nz).flags = 0;
            }
        }
    }
}

// Place a column-shaped canopy (birch).
// Tall narrow cylinder of leaves.
void place_canopy_column(
    TerrainData& terrain, int cx, int base_cy, int cz,
    int radius, int height, TerrainMaterialId leaves_mat) {
    for (int dy = 0; dy < height; ++dy) {
        const int cy = base_cy + dy;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) + std::abs(dz) > radius) continue;
                const int nx = cx + dx;
                const int ny = cy;
                const int nz = cz + dz;
                if (!terrain.is_valid_cell(nx, ny, nz)) continue;
                if (terrain.cell_at(nx, ny, nz).material != 0) continue;
                terrain.cell_at(nx, ny, nz).material =
                    static_cast<TerrainMaterial>(leaves_mat);
                terrain.cell_at(nx, ny, nz).flags = 0;
            }
        }
    }
}

} // namespace

void TerrainGenerator::pass_surface_objects(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain,
    std::vector<BlockEntityPlacement>& block_entities) {
    // Collect tree species that can appear in this dimension.
    std::vector<const TreeSpeciesDef*> species_list;
    for (const auto& species : config_->tree_species) {
        species_list.push_back(&species);
    }
    if (species_list.empty()) {
        return;
    }

    const auto mat = materials();
    NoiseGenerator tree_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT), dimension_id));
    NoiseGenerator canopy_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT) + 1, dimension_id));

    // Temperature and humidity noise (same seeds as pass_biome for consistency).
    NoiseGenerator temp_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BIOME), dimension_id));
    NoiseGenerator humidity_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BIOME) + 1, dimension_id));

    // Species selection noise (deterministic per-position species choice).
    NoiseGenerator species_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT) + 2, dimension_id));

    // Trunk height variation noise.
    NoiseGenerator height_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT) + 3, dimension_id));

    // Counter for generating unique entity IDs within this chunk.
    uint64_t next_entity_id = 1;

    for (int z = 0; z < terrain.size_z; ++z) {
        for (int x = 0; x < terrain.size_x; ++x) {
            int ground_y = -1;
            for (int y = terrain.size_y - 1; y >= 0; --y) {
                const TerrainCell& cell = terrain.cell_at(x, y, z);
                if (is_material(cell, mat.dirt) && is_exposed_to_air(terrain, x, y, z, mat.air)) {
                    ground_y = y;
                    break;
                }
            }
            if (ground_y < 0) {
                continue;
            }

            const int global_x = global_coord(chunk_x, x);
            const int global_y = global_coord(chunk_y, ground_y);
            const int global_z = global_coord(chunk_z, z);

            // Check tree density threshold.
            const float tree_density = tree_noise.noise_3d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_y),
                static_cast<float>(global_z),
                0.12f, 3);
            if (tree_density < 0.54f) {
                continue;
            }

            // Avoid placing trees near water.
            bool near_water = false;
            for (int dz = -2; dz <= 2 && !near_water; ++dz) {
                for (int dx = -2; dx <= 2 && !near_water; ++dx) {
                    for (int dy = -1; dy <= 1 && !near_water; ++dy) {
                        const int nx = x + dx;
                        const int ny = ground_y + dy;
                        const int nz = z + dz;
                        if (terrain.is_valid_cell(nx, ny, nz) &&
                            is_material(terrain.cell_at(nx, ny, nz), mat.water)) {
                            near_water = true;
                        }
                    }
                }
            }
            if (near_water) {
                continue;
            }

            // Compute temperature and humidity at this position.
            const float temperature = temp_noise.noise_3d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_y),
                static_cast<float>(global_z),
                0.015f, 3);
            const float humidity = humidity_noise.noise_3d_scaled(
                static_cast<float>(global_x + 2000),
                static_cast<float>(global_y + 2000),
                static_cast<float>(global_z + 2000),
                0.02f, 3);

            // Find matching species for this biome.
            std::vector<const TreeSpeciesDef*> matching;
            float total_weight = 0.0f;
            for (const auto* species : species_list) {
                if (temperature >= species->temperature_min &&
                    temperature <= species->temperature_max &&
                    humidity >= species->humidity_min &&
                    humidity <= species->humidity_max) {
                    matching.push_back(species);
                    total_weight += species->density_weight;
                }
            }
            if (matching.empty()) {
                continue;
            }

            // Select species by weighted random using species noise.
            const float species_val = species_noise.noise_3d_scaled(
                static_cast<float>(global_x + 7000),
                static_cast<float>(global_y + 7000),
                static_cast<float>(global_z + 7000),
                0.5f, 2);
            float threshold = (species_val * 0.5f + 0.5f) * total_weight;
            const TreeSpeciesDef* selected = matching[0];
            for (const auto* species : matching) {
                threshold -= species->density_weight;
                if (threshold <= 0.0f) {
                    selected = species;
                    break;
                }
            }

            // Resolve material IDs for this species.
            const TerrainMaterialId wood_mat = config_->material_id_or(
                selected->wood_material_key, mat.wood);
            const TerrainMaterialId leaves_mat = config_->material_id_or(
                selected->leaves_material_key, mat.leaves);

            // Determine trunk height with noise variation.
            const float height_val = height_noise.noise_3d_scaled(
                static_cast<float>(global_x + 9000),
                static_cast<float>(global_y + 9000),
                static_cast<float>(global_z + 9000),
                0.5f, 2);
            const int trunk_range = selected->max_trunk_height - selected->min_trunk_height;
            const int trunk_height = selected->min_trunk_height +
                static_cast<int>((height_val * 0.5f + 0.5f) * static_cast<float>(trunk_range));

            // Check that there is enough vertical space.
            if (ground_y + trunk_height + 3 >= terrain.size_y) {
                continue;
            }

            // Place trunk blocks.
            std::vector<OwnedCell> owned_cells;
            for (int trunk = 1; trunk <= trunk_height; ++trunk) {
                set_cell_id(terrain, x, ground_y + trunk, z, wood_mat);
                owned_cells.push_back({global_x, global_y + trunk, global_z});
            }

            // Place canopy based on species shape.
            const int canopy_base_y = ground_y + trunk_height + 1;
            switch (selected->canopy_shape) {
                case CanopyShape::SPHERE:
                    place_canopy_sphere(terrain, x, canopy_base_y, z,
                                       selected->canopy_radius, leaves_mat);
                    break;
                case CanopyShape::CONE: {
                    const int cone_height = selected->canopy_radius + 1;
                    place_canopy_cone(terrain, x, canopy_base_y, z,
                                     selected->canopy_radius, cone_height, leaves_mat);
                    break;
                }
                case CanopyShape::UMBRELLA:
                    place_canopy_umbrella(terrain, x, canopy_base_y, z,
                                         selected->canopy_radius, leaves_mat);
                    break;
                case CanopyShape::COLUMN: {
                    const int column_height = 3;
                    place_canopy_column(terrain, x, canopy_base_y, z,
                                        selected->canopy_radius, column_height, leaves_mat);
                    break;
                }
                default:
                    place_canopy_sphere(terrain, x, canopy_base_y, z,
                                       selected->canopy_radius, leaves_mat);
                    break;
            }

            // Collect canopy cells into owned_cells.
            // Scan the canopy area for leaves placed by this tree.
            const int scan_radius = selected->canopy_radius + 1;
            const int scan_top = (selected->canopy_shape == CanopyShape::CONE)
                ? canopy_base_y + selected->canopy_radius + 1
                : (selected->canopy_shape == CanopyShape::COLUMN)
                    ? canopy_base_y + 3
                    : canopy_base_y + 2;
            for (int sy = canopy_base_y - 1; sy <= scan_top; ++sy) {
                for (int sz = -scan_radius; sz <= scan_radius; ++sz) {
                    for (int sx = -scan_radius; sx <= scan_radius; ++sx) {
                        const int nx = x + sx;
                        const int ny = sy;
                        const int nz = z + sz;
                        if (!terrain.is_valid_cell(nx, ny, nz)) continue;
                        if (is_material(terrain.cell_at(nx, ny, nz), leaves_mat)) {
                            owned_cells.push_back({
                                global_x + sx,
                                global_y + (ny - ground_y),
                                global_z + sz});
                        }
                    }
                }
            }

            // Create a BlockEntityPlacement for this tree.
            BlockEntityPlacement be;
            be.id = EntityId{next_entity_id};
            ++next_entity_id;
            be.entity_type = BlockEntityType::TREE;
            be.root_x = global_x;
            be.root_y = global_y + 1;
            be.root_z = global_z;
            be.type_data_json =
                selected->species_key + "|" +
                std::to_string(static_cast<int>(TreeGrowthStage::MATURE)) + "|" +
                std::to_string(static_cast<int64_t>(0));
            be.owned_cell_count = static_cast<uint32_t>(owned_cells.size());
            block_entities.push_back(std::move(be));
        }
    }

    // --- Wild crop placement (Tier 1 wild foraging) ---
    // Places mature wild crops on dirt surfaces for players to mine for seeds.
    // Wild crops are terrain-only (no BlockEntity) 鈥?they don't grow and are
    // mined rather than harvested. This gives players a seed source to start
    // farming. Uses the "random tick" conceptual category (static placement).
    place_wild_crops(dimension_id, chunk_x, chunk_y, chunk_z, terrain);
}


void TerrainGenerator::place_wild_crops(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain) {
    // Collect wild crop species from the config.
    auto wild_crops = config_->wild_crop_species();
    if (wild_crops.empty()) {
        return;
    }

    const auto mat = materials();

    // Dedicated noise for wild crop density (distinct seed offset from trees).
    NoiseGenerator crop_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT) + 10,
        dimension_id));

    // Reuse biome noise (same seeds as pass_biome / tree pass) for species
    // selection consistency.
    NoiseGenerator temp_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BIOME), dimension_id));
    NoiseGenerator humidity_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::BIOME) + 1, dimension_id));
    NoiseGenerator species_noise(world_seed_.dimension_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT) + 11,
        dimension_id));

    for (int z = 0; z < terrain.size_z; ++z) {
        for (int x = 0; x < terrain.size_x; ++x) {
            // Find the topmost dirt block exposed to air.
            int ground_y = -1;
            for (int y = terrain.size_y - 1; y >= 0; --y) {
                const TerrainCell& cell = terrain.cell_at(x, y, z);
                if (is_material(cell, mat.dirt) &&
                    is_exposed_to_air(terrain, x, y, z, mat.air)) {
                    ground_y = y;
                    break;
                }
            }
            if (ground_y < 0) {
                continue;
            }

            // The air cell above the dirt is where the crop would go.
            const int crop_y = ground_y + 1;
            if (crop_y >= terrain.size_y) {
                continue;
            }
            if (!is_material(terrain.cell_at(x, crop_y, z), mat.air)) {
                continue;  // Already occupied (e.g. by a tree trunk).
            }

            const int global_x = global_coord(chunk_x, x);
            const int global_y = global_coord(chunk_y, ground_y);
            const int global_z = global_coord(chunk_z, z);

            // Wild crop density: rarer than trees (threshold 0.78 vs 0.54).
            const float crop_density = crop_noise.noise_3d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_y),
                static_cast<float>(global_z),
                0.15f, 3);
            if (crop_density < 0.78f) {
                continue;
            }

            // Compute biome conditions.
            const float temperature = temp_noise.noise_3d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_y),
                static_cast<float>(global_z),
                0.015f, 3);
            const float humidity = humidity_noise.noise_3d_scaled(
                static_cast<float>(global_x + 2000),
                static_cast<float>(global_y + 2000),
                static_cast<float>(global_z + 2000),
                0.02f, 3);

            // Find matching wild crop species for this biome.
            std::vector<const CropSpeciesDef*> matching;
            for (const auto* species : wild_crops) {
                if (temperature >= species->temperature_min &&
                    temperature <= species->temperature_max &&
                    humidity >= species->humidity_min &&
                    humidity <= species->humidity_max) {
                    matching.push_back(species);
                }
            }
            if (matching.empty()) {
                continue;
            }

            // Deterministic species selection.
            const float sel = species_noise.noise_3d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_y),
                static_cast<float>(global_z),
                1.0f, 3);
            const size_t idx = static_cast<size_t>(
                sel * static_cast<float>(matching.size())) % matching.size();
            const CropSpeciesDef* selected = matching[idx];

            // Resolve the mature-stage material.
            const TerrainMaterialId mature_mat = config_->material_id_or(
                selected->stage_material_keys[3], 0);
            if (mature_mat <= 0) {
                continue;
            }

            // Place the wild crop (mature stage) in the air cell above dirt.
            set_cell_id(terrain, x, crop_y, z, mature_mat);
        }
    }
}

// --- Pass 5: Gameplay ---

void TerrainGenerator::pass_gameplay(
    const std::string& dimension_id,
    int chunk_x, int chunk_y, int chunk_z,
    ChunkData& chunk) {
    (void)dimension_id;
    (void)chunk_x;
    (void)chunk_y;
    (void)chunk_z;
    (void)chunk;
}

// --- Helper methods ---

void TerrainGenerator::set_cell(
    TerrainData& terrain, int x, int y, int z, TerrainMaterial material) {
    set_cell_id(terrain, x, y, z, static_cast<TerrainMaterialId>(material));
}

void TerrainGenerator::set_cell_id(
    TerrainData& terrain, int x, int y, int z, TerrainMaterialId material) {
    TerrainCell& cell = terrain.cell_at(x, y, z);
    cell.material = static_cast<TerrainMaterial>(material);
    cell.flags = flags_for_material_id(material);
}

uint32_t TerrainGenerator::flags_for_material(TerrainMaterial material) const {
    return flags_for_material_id(static_cast<TerrainMaterialId>(material));
}

uint32_t TerrainGenerator::flags_for_material_id(TerrainMaterialId material) const {
    return config_->flags_for_material(material);
}

TerrainMaterialId TerrainGenerator::cell_material_id(const TerrainCell& cell) const {
    return static_cast<TerrainMaterialId>(cell.material);
}

bool TerrainGenerator::is_stone_cell(const TerrainCell& cell) const {
    return is_material(cell, materials().stone);
}

bool TerrainGenerator::is_material(
    const TerrainCell& cell, TerrainMaterialId material) const {
    return cell_material_id(cell) == material;
}

bool TerrainGenerator::is_walkable_ground_cell(const TerrainCell& cell) const {
    return config_->is_walkable_ground(cell_material_id(cell));
}

bool TerrainGenerator::has_near_material(
    const TerrainData& terrain, int x, int y, int z,
    TerrainMaterialId material, int radius) const {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                const int nz = z + dz;
                if (terrain.is_valid_cell(nx, ny, nz) &&
                    is_material(terrain.cell_at(nx, ny, nz), material)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool TerrainGenerator::has_floor_support(
    const TerrainData& terrain, int x, int y, int z,
    TerrainMaterialId support_material) const {
    for (int dy = 1; dy <= 2; ++dy) {
        const int ny = y - dy;
        if (terrain.is_valid_cell(x, ny, z) &&
            is_material(terrain.cell_at(x, ny, z), support_material)) {
            return true;
        }
    }
    return false;
}

TerrainGenerator::MaterialIds TerrainGenerator::materials() const {
    MaterialIds ids;
    ids.air = config_->roles.air;
    ids.stone = config_->roles.stone;
    ids.dirt = config_->roles.dirt;
    ids.sand = config_->roles.sand;
    ids.water = config_->roles.water;
    ids.lava = config_->roles.lava;
    ids.wood = config_->roles.wood;
    ids.leaves = config_->roles.leaves;
    ids.deepstone = config_->roles.deepstone;
    ids.core_barrier = config_->roles.core_barrier;
    ids.snow = config_->roles.snow;
    ids.ice = config_->roles.ice;
    return ids;
}

// --- Flat world helpers ---

int TerrainGenerator::surface_height_at(
    const NoiseGenerator& elevation_noise,
    int global_x, int global_z,
    const BaseTerrainRule& rule) const {
    if (std::abs(global_x) <= 4 && std::abs(global_z) <= 4) {
        return 0;
    }

    const float elevation = elevation_noise.noise_3d_scaled(
        static_cast<float>(global_x),
        0.0f,
        static_cast<float>(global_z),
        rule.elevation_scale, rule.elevation_octaves);
    const float broad = elevation_noise.noise_3d_scaled(
        static_cast<float>(global_x + 60000),
        0.0f,
        static_cast<float>(global_z - 60000),
        rule.elevation_scale * 0.35f, std::max(1, rule.elevation_octaves - 1));

    return static_cast<int>(std::round(elevation * 9.0f + broad * 14.0f));
}

float TerrainGenerator::cave_noise_at(
    const NoiseGenerator& cave_noise,
    int global_x, int global_y, int global_z,
    const BaseTerrainRule& rule) const {
    const float horizontal = cave_noise.noise_3d_scaled(
        static_cast<float>(global_x),
        static_cast<float>(global_y),
        static_cast<float>(global_z),
        rule.cave_scale, rule.cave_octaves);
    const float vertical = cave_noise.noise_3d_scaled(
        static_cast<float>(global_x + 30000),
        static_cast<float>(global_y + 30000),
        static_cast<float>(global_z - 30000),
        rule.cave_scale * 1.35f, rule.cave_octaves);
    return horizontal * 0.55f + vertical * 0.45f;
}

// --- Planet helpers ---

TerrainGenerator::LandformSample TerrainGenerator::sample_planet_landform(
    const NoiseGenerator& elevation_noise,
    const NoiseGenerator& detail_noise,
    float dir_x, float dir_y, float dir_z,
    const PlanetConfig& planet) const {
    // Convert the normalized direction back to surface-space coordinates.
    // Planet noise scales are specified in blocks; sampling the unit vector
    // directly made these scales nearly constant over the entire sphere.
    const float surface_x = dir_x * planet.planet_radius;
    const float surface_y = dir_y * planet.planet_radius;
    const float surface_z = dir_z * planet.planet_radius;

    const float elevation = elevation_noise.noise_3d_scaled(
        surface_x, surface_y, surface_z,
        planet.elevation_noise_scale, planet.elevation_octaves);
    const float detail = detail_noise.noise_3d_scaled(
        surface_x + 10000.0f, surface_y - 10000.0f, surface_z + 10000.0f,
        planet.detail_noise_scale, planet.detail_octaves);

    const float base_normalized = elevation * 0.7f + detail * 0.3f;
    const float base_offset = base_normalized * planet.terrain_height_scale;

    // Water-bearing planets keep low continental regions as ocean. Every
    // remaining direction is classified as land by the zone anchors below.
    const float ocean_threshold = planet.sea_level_fraction - 0.30f;
    if (planet.sea_level_fraction > 0.01f
        && base_normalized <= ocean_threshold) {
        return {LandformZone::BASIN, base_offset};
    }

    const float region_scale = std::max(
        0.0015f, planet.elevation_noise_scale * 0.2f);
    const float selector_raw = elevation_noise.noise_3d_scaled(
        surface_x + 37000.0f, surface_y - 19000.0f, surface_z + 53000.0f,
        region_scale, 2);
    const float selector = std::clamp(selector_raw * 1.8f, -1.0f, 1.0f);

    // Ridged noise turns zero-crossing contours into connected mountain
    // chains. A second, sharper field adds individual summits along them.
    const float ridge_source = detail_noise.noise_3d_scaled(
        surface_x - 23000.0f, surface_y + 41000.0f, surface_z - 17000.0f,
        std::max(0.01f, planet.detail_noise_scale * 0.55f), 3);
    const float ridge = std::pow(std::clamp(
        1.0f - std::abs(ridge_source) * 1.7f, 0.0f, 1.0f), 3.0f);
    const float peak_source = elevation_noise.noise_3d_scaled(
        surface_x + 71000.0f, surface_y + 29000.0f, surface_z - 61000.0f,
        std::max(0.018f, planet.detail_noise_scale), 3);
    const float peaks = std::pow(
        std::max(0.0f, peak_source), 2.0f);

    // Each profile is normalized by terrain_height_scale. Anchors are ordered
    // so every land point belongs to a zone and transitions remain continuous.
    constexpr float kAnchors[] = {-0.85f, -0.50f, -0.15f, 0.20f, 0.55f, 0.85f};
    const float profiles[] = {
        // Basin: low, gently rolling inland depressions.
        planet.sea_level_fraction + 0.04f + elevation * 0.08f + detail * 0.03f,
        // Plains: broad areas with only a few blocks of relief.
        planet.sea_level_fraction + 0.12f + elevation * 0.10f + detail * 0.04f,
        // Hills: medium-amplitude rolling terrain.
        planet.sea_level_fraction + 0.18f
            + elevation * 0.38f + detail * 0.12f,
        // Plateau: raised but locally flat terrain.
        planet.sea_level_fraction + 0.35f
            + elevation * 0.08f + detail * 0.04f,
        // Mountains: broad range uplift plus connected ridges and summits.
        planet.sea_level_fraction + 0.26f + ridge * 0.68f
            + peaks * 0.42f + elevation * 0.12f + detail * 0.06f,
        // Rugged land: maximum local relief, common on barren bodies.
        planet.sea_level_fraction + 0.16f
            + elevation * 0.65f + detail * 0.35f + peaks * 0.18f,
    };

    int lower = 0;
    while (lower < 5 && selector > kAnchors[lower + 1]) {
        ++lower;
    }
    int upper = std::min(lower + 1, 5);
    float blend = 0.0f;
    if (upper != lower) {
        blend = std::clamp(
            (selector - kAnchors[lower])
                / (kAnchors[upper] - kAnchors[lower]),
            0.0f, 1.0f);
        blend = blend * blend * (3.0f - 2.0f * blend);
    }

    float normalized_offset =
        profiles[lower] + (profiles[upper] - profiles[lower]) * blend;

    // Atmospheres erode terrain. Breathable worlds favor smoother land while
    // airless bodies preserve sharper relief without changing zone coverage.
    float erosion_multiplier = 1.0f;
    if (planet.atmosphere_type == ATMO_BREATHABLE) {
        erosion_multiplier = 0.78f;
    } else if (planet.atmosphere_type == ATMO_TOXIC
               || planet.atmosphere_type == ATMO_CORROSIVE) {
        erosion_multiplier = 0.90f;
    } else if (planet.atmosphere_type == ATMO_NONE) {
        erosion_multiplier = 1.12f;
    }
    normalized_offset = planet.sea_level_fraction
        + (normalized_offset - planet.sea_level_fraction) * erosion_multiplier;

    // Land must remain above sea level after profile blending.
    if (planet.sea_level_fraction > 0.01f) {
        normalized_offset = std::max(
            normalized_offset, planet.sea_level_fraction + 0.03f);
    }

    const int nearest = (upper != lower && blend >= 0.5f) ? upper : lower;
    return {
        static_cast<LandformZone>(nearest),
        normalized_offset * planet.terrain_height_scale,
    };
}

float TerrainGenerator::planet_surface_radius(
    const NoiseGenerator& elevation_noise,
    const NoiseGenerator& detail_noise,
    float dir_x, float dir_y, float dir_z,
    const PlanetConfig& planet) const {
    const LandformSample landform = sample_planet_landform(
        elevation_noise, detail_noise, dir_x, dir_y, dir_z, planet);

    return planet.planet_radius + landform.terrain_offset;
}

} // namespace science_and_theology
