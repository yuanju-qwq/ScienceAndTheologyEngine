#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../defs/resource_types.h"
#include "../defs/terrain_data.h"
#include "../defs/tree_species_def.h"
#include "../defs/crop_species_def.h"

namespace snt::data {

struct TerrainDropDef {
    std::string item_key;
    ItemId item_id = kInvalidItemId;
    int count = 1;
    int min_count = 1;
    int max_count = 1;
    float chance = 1.0f;
};

struct TerrainMaterialDef {
    TerrainMaterialId id = 0;
    std::string key;
    std::string title_key;
    uint32_t flags = 0;
    float hardness = 1.0f;
    std::string required_tool_tag;
    int required_mining_level = 0;
    std::vector<TerrainDropDef> drops;

    // Gravity behavior: block falls when unsupported (e.g., sand, gravel).
    // Derived from TF_GRAVITY_FALL flag at registration time.
    bool gravity_fall = false;

    // Collapse behavior: block can cave-in when structural support is lost.
    // Derived from TF_COLLAPSE_RISK flag at registration time.
    bool collapse_risk = false;

    // Base probability of collapse when unsupported (0.0 = never, 1.0 = always).
    // Multiplied by GameplayConfig::collapse_chance_multiplier at runtime.
    float collapse_chance = 0.3f;

    // If this block is a support beam (TF_SUPPORT_BEAM), how far it reaches.
    // Measured in blocks along the gravity direction.
    int support_radius = 5;

    // Rock layer key: which rock layer this material belongs to.
    // Empty string means no rock layer association.
    std::string rock_layer_key;
};

// Face-specific texture reference for one face group of a voxel material.
struct TerrainFaceTexture {
    std::string texture_path;
    int variant_count = 1;
};

// Overlay layer: a texture blended on top of the base face textures.
// Used for ore veins, moss, cracks, etc. stacked in order (first = bottom).
struct TerrainOverlayLayer {
    std::string texture_path;
    float blend = 0.5f;
};

// Visual definition for a single terrain material in 3D rendering.
struct TerrainMaterialVisualDef {
    TerrainMaterialId material_id = 0;
    std::string material_key;
    std::string dimension_id = "overworld";
    bool enabled = true;

    // Per-face textures (empty texture_path = use albedo_color fallback).
    TerrainFaceTexture top;
    TerrainFaceTexture bottom;
    TerrainFaceTexture sides;

    // Fallback color when no texture is assigned.
    float albedo_r = 0.85f;
    float albedo_g = 0.20f;
    float albedo_b = 0.85f;
    float albedo_a = 1.0f;

    // Material properties.
    float roughness = 0.92f;
    float emissive_r = 0.0f;
    float emissive_g = 0.0f;
    float emissive_b = 0.0f;
    bool transparent = false;
    bool cull_disabled = false;

    // Overlay layers blended on top of base face textures (bottom-up order).
    std::vector<TerrainOverlayLayer> overlays;
};

struct TerrainMaterialRoles {
    TerrainMaterialId air = 0;
    TerrainMaterialId stone = 0;
    TerrainMaterialId dirt = 0;
    TerrainMaterialId sand = 0;
    TerrainMaterialId water = 0;
    TerrainMaterialId lava = 0;
    TerrainMaterialId ore_iron = 0;
    TerrainMaterialId ore_copper = 0;
    TerrainMaterialId ore_coal = 0;
    TerrainMaterialId wood = 0;
    TerrainMaterialId leaves = 0;
    TerrainMaterialId deepstone = 0;
    TerrainMaterialId core_barrier = 0;
    TerrainMaterialId snow = 0;
    TerrainMaterialId ice = 0;
};

// Runtime material IDs for blocks placed by players, NOT by terrain generation.
// These are resolved from the same material registry but are not consumed
// by any terrain pass. The command server uses these to write terrain cells.
struct RuntimeMaterialIds {
    TerrainMaterialId ladder = 0;
    TerrainMaterialId workbench = 0;
    TerrainMaterialId fence = 0;
    TerrainMaterialId farmland = 0;
    TerrainMaterialId bloomery = 0;
};

struct BaseTerrainRule {
    std::string dimension_id = "overworld";
    std::string mode = "solid";
    TerrainMaterialId default_material = 0;
    TerrainMaterialId low_elevation_material = 0;
    TerrainMaterialId high_elevation_material = 0;
    TerrainMaterialId cave_air_material = 0;
    float elevation_scale = 0.02f;
    int elevation_octaves = 4;
    float detail_scale = 0.05f;
    int detail_octaves = 3;
    float water_elevation_max = -0.25f;
    float water_detail_max = 0.3f;
    float stone_elevation_abs_min = 0.55f;
    float cave_scale = 0.04f;
    int cave_octaves = 4;
    float cave_threshold = 0.35f;
    float cave_edge_threshold_add = 0.25f;
};

struct BiomeRule {
    std::string key;
    std::string dimension_id = "overworld";
    TerrainMaterialId source_material = 0;
    TerrainMaterialId result_material = 0;
    std::string condition = "temperature_humidity";
    float temperature_min = -1.0f;
    float temperature_max = 1.0f;
    float humidity_min = -1.0f;
    float humidity_max = 1.0f;
    bool requires_near_material = false;
    TerrainMaterialId near_material = 0;
    int near_radius = 2;
    bool requires_floor_support = false;
    TerrainMaterialId support_material = 0;
    float detail_scale = 0.1f;
    int detail_octaves = 2;
    float detail_threshold = -1.0f;
};

// Ore vein group: a GT-style multi-ore vein that contains primary, secondary,
// between, and sporadic ore materials. Each vein group occupies a spatial
// region defined by noise, with a defined depth range and density.
// Within a vein, the distribution follows:
//   - primary_ore:   ~40% of vein blocks (core of the vein)
//   - secondary_ore: ~30% of vein blocks (surrounding primary)
//   - between_ore:   ~20% of vein blocks (between primary clusters)
//   - sporadic_ore:  ~10% of vein blocks (rare scattered pockets)
struct OreVeinGroup {
    std::string key;
    std::string dimension_id = "overworld";

    // Host rock material that this vein replaces.
    TerrainMaterialId host_material = 0;

    // Primary ore: most abundant in the vein center.
    TerrainMaterialId primary_ore = 0;

    // Secondary ore: less abundant, surrounds primary clusters.
    TerrainMaterialId secondary_ore = 0;

    // Between ore: scattered between primary clusters.
    TerrainMaterialId between_ore = 0;

    // Sporadic ore: rare, small pockets within the vein.
    TerrainMaterialId sporadic_ore = 0;

    // Depth range: distance from the planet surface toward the core.
    // Only blocks within this depth range can host this vein group.
    float depth_min = 0.0f;
    float depth_max = 100.0f;

    // Vein radius: approximate horizontal extent of the vein in blocks.
    // Controls the noise scale for vein shape (larger = wider vein).
    float radius = 16.0f;

    // Density: probability that a block within the vein shape is replaced
    // by an ore material (0.0 = none, 1.0 = all blocks replaced).
    float density = 0.6f;

    // Weight: relative probability of this vein group being selected
    // when multiple vein groups could occupy the same position.
    // Higher weight = more common vein group.
    float weight = 1.0f;
};

// Rock layer: determines underground rock type per region on the planet surface.
// Rock layers are selected by noise-driven regional variation, so different
// areas of the planet have different base rock types (granite, basalt, etc.).
// Each rock layer defines its material, depth range, hardness, collapse
// properties, and which ores can spawn in it.
struct RockLayerRule {
    std::string key;
    std::string dimension_id = "overworld";

    // The rock material placed by this layer.
    TerrainMaterialId rock_material = 0;

    // Noise-driven region selection: this layer appears where the rock layer
    // noise falls within [noise_min, noise_max].
    float noise_scale = 0.005f;
    int noise_octaves = 3;
    float noise_min = -1.0f;
    float noise_max = 1.0f;

    // Depth range: distance from the planet surface toward the core.
    // depth_min = 0 means starting from the surface.
    // depth_max controls how deep this rock extends.
    float depth_min = 0.0f;
    float depth_max = 100.0f;

    // Physical properties of this rock layer.
    float hardness_multiplier = 1.0f;

    // Base probability of cave-in for blocks in this layer.
    // Multiplied by GameplayConfig::collapse_chance_multiplier at runtime.
    float collapse_chance = 0.3f;

    // Ores that can spawn in this rock layer (material IDs).
    std::vector<TerrainMaterialId> associated_ores;
};

// Atmosphere type 鈥?determines gameplay effects of a planet's atmosphere.
// Must match the AtmosphereType enum in PlanetDescriptor.gd.
enum AtmosphereType {
    ATMO_NONE = 0,       // Vacuum 鈥?no atmosphere (e.g., Mercury, asteroids).
    ATMO_THIN = 1,       // Thin atmosphere 鈥?oxygen mask required (e.g., Mars).
    ATMO_BREATHABLE = 2, // Breathable 鈥?safe without equipment (e.g., Earth).
    ATMO_TOXIC = 3,      // Toxic 鈥?continuous damage without suit (e.g., Venus).
    ATMO_CORROSIVE = 4,  // Corrosive 鈥?damage + equipment degradation (e.g., acid world).
};

// Planet configuration for spherical world generation.
// Each dimension can optionally be a planet with a defined radius and center.
// When planet_radius > 0, the terrain generator uses spherical clipping
// instead of the flat infinite-plane model.
struct PlanetConfig {
    std::string dimension_id = "overworld";

    // Radius of the planet in voxel blocks. 0 means flat world (no planet).
    float planet_radius = 0.0f;

    // Center of the planet in world voxel coordinates.
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;

    // Maximum terrain displacement from the base sphere surface.
    // Controls mountain height and ocean depth.
    float terrain_height_scale = 16.0f;

    // Noise scale for the 3D elevation noise applied on the sphere.
    float elevation_noise_scale = 0.008f;

    // Number of octaves for the elevation noise.
    int elevation_octaves = 5;

    // Noise scale for detail/roughness on the sphere surface.
    float detail_noise_scale = 0.03f;

    // Number of octaves for detail noise.
    int detail_octaves = 3;

    // Noise scale for cave generation inside the planet.
    float cave_noise_scale = 0.04f;

    // Number of octaves for cave noise.
    int cave_octaves = 4;

    // Threshold for cave generation. Higher = fewer caves.
    float cave_threshold = 0.35f;

    // Sea level as a fraction of terrain_height_scale above the base radius.
    // 0.0 = sea at base radius, 1.0 = sea at max terrain height.
    float sea_level_fraction = 0.3f;

    // Core radius as a fraction of planet_radius.
    // Blocks within this radius are indestructible core_barrier material.
    // Prevents players from reaching the gravity singularity at the center.
    float core_radius_ratio = 0.05f;

    // Mantle radius as a fraction of planet_radius.
    // Between core and mantle: outer core (lava zone).
    // Between mantle and surface: crust (stone + caves + ores).
    float mantle_radius_ratio = 0.5f;

    // Noise scale for perturbing the core boundary.
    // Makes the core shape irregular instead of a perfect sphere.
    float core_boundary_noise_scale = 0.02f;

    // Number of octaves for core boundary noise.
    int core_boundary_noise_octaves = 3;

    // Amplitude of core boundary noise as a fraction of core radius.
    // 0.15 = core boundary can deviate by 卤15% of core radius.
    float core_boundary_noise_amplitude = 0.15f;

    // Atmosphere type 鈥?determines environmental hazards.
    // See AtmosphereType enum for values.
    int atmosphere_type = ATMO_BREATHABLE;

    bool is_planet() const { return planet_radius > 0.0f; }
};

struct WorldGenConfigSnapshot {
    static constexpr uint32_t kSchemaVersion = 9;

    uint32_t schema_version = kSchemaVersion;
    uint64_t content_hash = 0;
    std::vector<TerrainMaterialDef> materials;
    std::vector<TerrainMaterialVisualDef> material_visuals;
    TerrainMaterialRoles roles;
    RuntimeMaterialIds runtime_ids;
    std::vector<BaseTerrainRule> base_terrain_rules;
    std::vector<BiomeRule> biome_rules;
    std::vector<OreVeinGroup> ore_vein_groups;
    std::vector<RockLayerRule> rock_layer_rules;
    std::vector<PlanetConfig> planet_configs;
    std::vector<TreeSpeciesDef> tree_species;
    std::vector<CropSpeciesDef> crop_species;
    std::unordered_map<std::string, TerrainMaterialId> material_ids_by_key;
    std::unordered_map<int, std::string> material_keys_by_id;

    const TerrainMaterialDef* find_material(TerrainMaterialId id) const;
    const TerrainMaterialDef* find_material(const std::string& key) const;
    TerrainMaterialId material_id_or(const std::string& key, TerrainMaterialId fallback) const;
    uint32_t flags_for_material(TerrainMaterialId id) const;
    bool has_material(TerrainMaterialId id) const;
    bool has_material_key(const std::string& key) const;
    bool is_role(TerrainMaterialId id, TerrainMaterialId role_id) const;
    bool is_walkable_ground(TerrainMaterialId id) const;
    const BaseTerrainRule* find_base_rule(const std::string& dimension_id) const;
    const PlanetConfig* find_planet_config(const std::string& dimension_id) const;

    // Find a tree species by its key. Returns nullptr if not found.
    const TreeSpeciesDef* find_tree_species(const std::string& species_key) const;

    // Returns all tree species that can grow in the given temperature/humidity.
    std::vector<const TreeSpeciesDef*> tree_species_for_biome(
        float temperature, float humidity) const;

    // Find a crop species by its key. Returns nullptr if not found.
    const CropSpeciesDef* find_crop_species(const std::string& species_key) const;

    // Find a crop species by its seed item key. Returns nullptr if not found.
    const CropSpeciesDef* find_crop_by_seed(const std::string& seed_item_key) const;

    // Returns all crop species flagged for wild generation.
    std::vector<const CropSpeciesDef*> wild_crop_species() const;

    // Returns all crop species that can grow in the given temperature/humidity.
    std::vector<const CropSpeciesDef*> crop_species_for_biome(
        float temperature, float humidity) const;
};

std::shared_ptr<const WorldGenConfigSnapshot> make_empty_world_gen_config();
uint64_t hash_world_gen_config(const WorldGenConfigSnapshot& config);

} // namespace science_and_theology
