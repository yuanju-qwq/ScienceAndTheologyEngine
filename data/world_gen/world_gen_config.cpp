#include "world_gen_config.h"

#include <algorithm>
#include <functional>

namespace snt::data {
namespace {

void hash_combine(uint64_t& seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

} // namespace

const TerrainMaterialDef* WorldGenConfigSnapshot::find_material(
    TerrainMaterialId id) const {
    auto it = std::find_if(materials.begin(), materials.end(),
        [id](const TerrainMaterialDef& def) {
            return def.id == id;
        });
    return it != materials.end() ? &(*it) : nullptr;
}

const TerrainMaterialDef* WorldGenConfigSnapshot::find_material(
    const std::string& key) const {
    auto id_it = material_ids_by_key.find(key);
    if (id_it == material_ids_by_key.end()) {
        return nullptr;
    }
    return find_material(id_it->second);
}

TerrainMaterialId WorldGenConfigSnapshot::material_id_or(
    const std::string& key, TerrainMaterialId fallback) const {
    auto id_it = material_ids_by_key.find(key);
    if (id_it == material_ids_by_key.end()) {
        return fallback;
    }
    return id_it->second;
}

uint32_t WorldGenConfigSnapshot::flags_for_material(TerrainMaterialId id) const {
    if (const auto* def = find_material(id)) {
        return def->flags;
    }
    return 0;
}

bool WorldGenConfigSnapshot::has_material(TerrainMaterialId id) const {
    return material_keys_by_id.find(id) != material_keys_by_id.end();
}

bool WorldGenConfigSnapshot::has_material_key(const std::string& key) const {
    return material_ids_by_key.find(key) != material_ids_by_key.end();
}

bool WorldGenConfigSnapshot::is_role(TerrainMaterialId id, TerrainMaterialId role_id) const {
    return id == role_id;
}

bool WorldGenConfigSnapshot::is_walkable_ground(TerrainMaterialId id) const {
    return id == roles.dirt || id == roles.sand;
}

const BaseTerrainRule* WorldGenConfigSnapshot::find_base_rule(
    const std::string& dimension_id) const {
    auto it = std::find_if(base_terrain_rules.begin(), base_terrain_rules.end(),
        [&dimension_id](const BaseTerrainRule& rule) {
            return rule.dimension_id == dimension_id;
        });
    return it != base_terrain_rules.end() ? &(*it) : nullptr;
}

const PlanetConfig* WorldGenConfigSnapshot::find_planet_config(
    const std::string& dimension_id) const {
    auto it = std::find_if(planet_configs.begin(), planet_configs.end(),
        [&dimension_id](const PlanetConfig& config) {
            return config.dimension_id == dimension_id;
        });
    return it != planet_configs.end() ? &(*it) : nullptr;
}

std::shared_ptr<const WorldGenConfigSnapshot> make_empty_world_gen_config() {
    auto config = std::make_shared<WorldGenConfigSnapshot>();
    config->content_hash = hash_world_gen_config(*config);
    return config;
}

uint64_t hash_world_gen_config(const WorldGenConfigSnapshot& config) {
    uint64_t hash = 1469598103934665603ULL;
    hash_combine(hash, config.schema_version);

    std::hash<std::string> string_hash;
    for (const auto& material : config.materials) {
        hash_combine(hash, material.id);
        hash_combine(hash, string_hash(material.key));
        hash_combine(hash, string_hash(material.title_key));
        hash_combine(hash, material.flags);
        hash_combine(hash, static_cast<uint64_t>(material.hardness * 1000.0f));
        hash_combine(hash, string_hash(material.required_tool_tag));
        hash_combine(hash, static_cast<uint64_t>(material.required_mining_level));
        hash_combine(hash, material.gravity_fall ? 1ULL : 0ULL);
        hash_combine(hash, material.collapse_risk ? 1ULL : 0ULL);
        hash_combine(hash, static_cast<uint64_t>(material.collapse_chance * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(material.support_radius));
        hash_combine(hash, string_hash(material.rock_layer_key));
        for (const auto& drop : material.drops) {
            hash_combine(hash, string_hash(drop.item_key));
            hash_combine(hash, static_cast<uint64_t>(drop.item_id));
            hash_combine(hash, static_cast<uint64_t>(drop.count));
            hash_combine(hash, static_cast<uint64_t>(drop.min_count));
            hash_combine(hash, static_cast<uint64_t>(drop.max_count));
            hash_combine(hash, static_cast<uint64_t>(drop.chance * 100000.0f));
        }
    }
    for (const auto& visual : config.material_visuals) {
        hash_combine(hash, visual.material_id);
        hash_combine(hash, string_hash(visual.material_key));
        hash_combine(hash, string_hash(visual.dimension_id));
        hash_combine(hash, visual.enabled ? 1ULL : 0ULL);
        hash_combine(hash, string_hash(visual.top.texture_path));
        hash_combine(hash, static_cast<uint64_t>(visual.top.variant_count));
        hash_combine(hash, string_hash(visual.bottom.texture_path));
        hash_combine(hash, static_cast<uint64_t>(visual.bottom.variant_count));
        hash_combine(hash, string_hash(visual.sides.texture_path));
        hash_combine(hash, static_cast<uint64_t>(visual.sides.variant_count));
        hash_combine(hash, static_cast<uint64_t>(visual.albedo_r * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(visual.albedo_g * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(visual.albedo_b * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(visual.albedo_a * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(visual.roughness * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(visual.emissive_r * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(visual.emissive_g * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(visual.emissive_b * 100000.0f));
        hash_combine(hash, visual.transparent ? 1ULL : 0ULL);
        hash_combine(hash, visual.cull_disabled ? 1ULL : 0ULL);
        for (const auto& overlay : visual.overlays) {
            hash_combine(hash, string_hash(overlay.texture_path));
            hash_combine(hash, static_cast<uint64_t>(overlay.blend * 100000.0f));
        }
    }
    hash_combine(hash, config.roles.air);
    hash_combine(hash, config.roles.stone);
    hash_combine(hash, config.roles.dirt);
    hash_combine(hash, config.roles.sand);
    hash_combine(hash, config.roles.water);
    hash_combine(hash, config.roles.lava);
    hash_combine(hash, config.roles.ore_iron);
    hash_combine(hash, config.roles.ore_copper);
    hash_combine(hash, config.roles.ore_coal);
    hash_combine(hash, config.roles.wood);
    hash_combine(hash, config.roles.leaves);
    hash_combine(hash, config.roles.deepstone);
    hash_combine(hash, config.roles.core_barrier);
    for (const auto& rule : config.base_terrain_rules) {
        hash_combine(hash, string_hash(rule.dimension_id));
        hash_combine(hash, string_hash(rule.mode));
        hash_combine(hash, rule.default_material);
        hash_combine(hash, rule.low_elevation_material);
        hash_combine(hash, rule.high_elevation_material);
        hash_combine(hash, rule.cave_air_material);
        hash_combine(hash, static_cast<uint64_t>(rule.elevation_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.elevation_octaves));
        hash_combine(hash, static_cast<uint64_t>(rule.detail_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.detail_octaves));
        hash_combine(hash, static_cast<uint64_t>((rule.water_elevation_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.water_detail_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.stone_elevation_abs_min * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.cave_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.cave_octaves));
        hash_combine(hash, static_cast<uint64_t>(rule.cave_threshold * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.cave_edge_threshold_add * 100000.0f));
    }
    for (const auto& rule : config.biome_rules) {
        hash_combine(hash, string_hash(rule.key));
        hash_combine(hash, string_hash(rule.dimension_id));
        hash_combine(hash, rule.source_material);
        hash_combine(hash, rule.result_material);
        hash_combine(hash, string_hash(rule.condition));
        hash_combine(hash, static_cast<uint64_t>((rule.temperature_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.temperature_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.humidity_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.humidity_max + 2.0f) * 100000.0f));
        hash_combine(hash, rule.requires_near_material ? 1ULL : 0ULL);
        hash_combine(hash, rule.near_material);
        hash_combine(hash, static_cast<uint64_t>(rule.near_radius));
        hash_combine(hash, rule.requires_floor_support ? 1ULL : 0ULL);
        hash_combine(hash, rule.support_material);
        hash_combine(hash, static_cast<uint64_t>(rule.detail_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.detail_octaves));
        hash_combine(hash, static_cast<uint64_t>((rule.detail_threshold + 2.0f) * 100000.0f));
    }
    for (const auto& group : config.ore_vein_groups) {
        hash_combine(hash, string_hash(group.key));
        hash_combine(hash, string_hash(group.dimension_id));
        hash_combine(hash, group.host_material);
        hash_combine(hash, group.primary_ore);
        hash_combine(hash, group.secondary_ore);
        hash_combine(hash, group.between_ore);
        hash_combine(hash, group.sporadic_ore);
        hash_combine(hash, static_cast<uint64_t>((group.depth_min + 100.0f) * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>((group.depth_max + 100.0f) * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(group.radius * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(group.density * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(group.weight * 100000.0f));
    }
    for (const auto& rule : config.rock_layer_rules) {
        hash_combine(hash, string_hash(rule.key));
        hash_combine(hash, string_hash(rule.dimension_id));
        hash_combine(hash, rule.rock_material);
        hash_combine(hash, static_cast<uint64_t>(rule.noise_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.noise_octaves));
        hash_combine(hash, static_cast<uint64_t>((rule.noise_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.noise_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.depth_min + 100.0f) * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.depth_max + 100.0f) * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.hardness_multiplier * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.collapse_chance * 100000.0f));
        for (const auto& ore : rule.associated_ores) {
            hash_combine(hash, ore);
        }
    }
    for (const auto& planet : config.planet_configs) {
        hash_combine(hash, string_hash(planet.dimension_id));
        hash_combine(hash, static_cast<uint64_t>(planet.planet_radius * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.center_x * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.center_y * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.center_z * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.terrain_height_scale * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.elevation_noise_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.elevation_octaves));
        hash_combine(hash, static_cast<uint64_t>(planet.detail_noise_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.detail_octaves));
        hash_combine(hash, static_cast<uint64_t>(planet.cave_noise_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.cave_octaves));
        hash_combine(hash, static_cast<uint64_t>(planet.cave_threshold * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.sea_level_fraction * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.core_radius_ratio * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.mantle_radius_ratio * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.core_boundary_noise_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.core_boundary_noise_octaves));
        hash_combine(hash, static_cast<uint64_t>(planet.core_boundary_noise_amplitude * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.atmosphere_type));
    }
    for (const auto& species : config.tree_species) {
        hash_combine(hash, string_hash(species.species_key));
        hash_combine(hash, string_hash(species.title_key));
        hash_combine(hash, static_cast<uint64_t>((species.temperature_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((species.temperature_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((species.humidity_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((species.humidity_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(species.density_weight * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(species.min_trunk_height));
        hash_combine(hash, static_cast<uint64_t>(species.max_trunk_height));
        hash_combine(hash, static_cast<uint64_t>(species.canopy_shape));
        hash_combine(hash, static_cast<uint64_t>(species.canopy_radius));
        hash_combine(hash, string_hash(species.wood_material_key));
        hash_combine(hash, string_hash(species.leaves_material_key));
        hash_combine(hash, string_hash(species.sapling_material_key));
        hash_combine(hash, species.is_evergreen ? 1ULL : 0ULL);
        hash_combine(hash, static_cast<uint64_t>(species.ticks_to_young));
        hash_combine(hash, static_cast<uint64_t>(species.ticks_to_mature));
        hash_combine(hash, species.has_fruit ? 1ULL : 0ULL);
        hash_combine(hash, string_hash(species.fruit_item_key));
        hash_combine(hash, static_cast<uint64_t>(species.fruit_season));
    }
    for (const auto& crop : config.crop_species) {
        hash_combine(hash, string_hash(crop.species_key));
        hash_combine(hash, string_hash(crop.title_key));
        hash_combine(hash, static_cast<uint64_t>(crop.category));
        hash_combine(hash, static_cast<uint64_t>((crop.temperature_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((crop.temperature_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((crop.humidity_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((crop.humidity_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(crop.plant_season + 1));
        hash_combine(hash, static_cast<uint64_t>(crop.grow_season + 1));
        hash_combine(hash, static_cast<uint64_t>(crop.harvest_season + 1));
        hash_combine(hash, static_cast<uint64_t>(crop.ticks_seed_to_sprout));
        hash_combine(hash, static_cast<uint64_t>(crop.ticks_sprout_to_growing));
        hash_combine(hash, static_cast<uint64_t>(crop.ticks_growing_to_mature));
        hash_combine(hash, string_hash(crop.seed_item_key));
        hash_combine(hash, string_hash(crop.crop_item_key));
        hash_combine(hash, string_hash(crop.byproduct_item_key));
        hash_combine(hash, static_cast<uint64_t>(crop.crop_min));
        hash_combine(hash, static_cast<uint64_t>(crop.crop_max));
        hash_combine(hash, static_cast<uint64_t>(crop.byproduct_count));
        hash_combine(hash, crop.repeat_harvest ? 1ULL : 0ULL);
        hash_combine(hash, static_cast<uint64_t>(crop.regrow_ticks));
        for (int i = 0; i < 4; ++i) {
            hash_combine(hash, string_hash(crop.stage_material_keys[i]));
        }
        hash_combine(hash, static_cast<uint64_t>(crop.fertility_sensitivity * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(crop.water_sensitivity * 100000.0f));
        hash_combine(hash, crop.wild_spawn ? 1ULL : 0ULL);
        hash_combine(hash, static_cast<uint64_t>(crop.wild_density_weight * 100000.0f));
    }
    return hash;
}

// --- Tree species lookup ---

const TreeSpeciesDef* WorldGenConfigSnapshot::find_tree_species(
    const std::string& species_key) const {
    auto it = std::find_if(tree_species.begin(), tree_species.end(),
        [&species_key](const TreeSpeciesDef& def) {
            return def.species_key == species_key;
        });
    return it != tree_species.end() ? &(*it) : nullptr;
}

std::vector<const TreeSpeciesDef*> WorldGenConfigSnapshot::tree_species_for_biome(
    float temperature, float humidity) const {
    std::vector<const TreeSpeciesDef*> result;
    for (const auto& species : tree_species) {
        if (temperature >= species.temperature_min &&
            temperature <= species.temperature_max &&
            humidity >= species.humidity_min &&
            humidity <= species.humidity_max) {
            result.push_back(&species);
        }
    }
    return result;
}

// --- Crop species lookup ---

const CropSpeciesDef* WorldGenConfigSnapshot::find_crop_species(
    const std::string& species_key) const {
    auto it = std::find_if(crop_species.begin(), crop_species.end(),
        [&species_key](const CropSpeciesDef& def) {
            return def.species_key == species_key;
        });
    return it != crop_species.end() ? &(*it) : nullptr;
}

const CropSpeciesDef* WorldGenConfigSnapshot::find_crop_by_seed(
    const std::string& seed_item_key) const {
    auto it = std::find_if(crop_species.begin(), crop_species.end(),
        [&seed_item_key](const CropSpeciesDef& def) {
            return def.seed_item_key == seed_item_key;
        });
    return it != crop_species.end() ? &(*it) : nullptr;
}

std::vector<const CropSpeciesDef*> WorldGenConfigSnapshot::wild_crop_species() const {
    std::vector<const CropSpeciesDef*> result;
    for (const auto& species : crop_species) {
        if (species.wild_spawn) {
            result.push_back(&species);
        }
    }
    return result;
}

std::vector<const CropSpeciesDef*> WorldGenConfigSnapshot::crop_species_for_biome(
    float temperature, float humidity) const {
    std::vector<const CropSpeciesDef*> result;
    for (const auto& species : crop_species) {
        if (temperature >= species.temperature_min &&
            temperature <= species.temperature_max &&
            humidity >= species.humidity_min &&
            humidity <= species.humidity_max) {
            result.push_back(&species);
        }
    }
    return result;
}

} // namespace science_and_theology
