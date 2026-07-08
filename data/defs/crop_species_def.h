// CropSpeciesDef — defines a crop species for farming.
//
// Ported from src/core/world_gen/crop_species_def.hpp.
// Namespace: science_and_theology -> snt::data.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace snt::data {

// ============================================================
// CropSpeciesDef — defines a crop species for farming
// ============================================================
//
// Each crop species has unique properties controlling:
//   - Where it grows (biome constraints via temperature/humidity)
//   - When it grows (season constraints)
//   - How it grows (ticks per stage, fertility/water sensitivity)
//   - What it produces (seed, crop, byproduct)
//   - How it looks (4 stage terrain materials)
//
// Species are registered in GDScript (BuiltinTerrainContent.gd) via
// GDTerrainContentRegistry.register_crop_species() and consumed by
// the C++ CropGrowthSystem and terrain generator.

// Crop growth stage. Herbaceous crops have 4 stages.
enum class CropGrowthStage : uint8_t {
    SEED        = 0,   // Just planted, not yet sprouted
    SPROUT      = 1,   // Sprouted
    GROWING     = 2,   // Growing
    MATURE      = 3,   // Mature, harvestable
    COUNT       = 4,
};

constexpr const char* kCropGrowthStageNames[] = {
    "Seed", "Sprout", "Growing", "Mature",
};

// Crop category: determines processing chain and quest placement.
enum class CropCategory : uint8_t {
    GRAIN       = 0,   // Grain (wheat, rice) -> flour/starch
    ROOT        = 1,   // Root (carrot, potato) -> direct food/starch
    FIBER       = 2,   // Fiber (cotton, flax) -> fiber/cloth
    HERB        = 3,   // Herb (mint, chamomile) -> potion
    FRUIT       = 4,   // Fruit (pumpkin, berry) -> food/fermentation
    MAGIC       = 5,   // Source plant (Tier 5)
    COUNT       = 6,
};

constexpr const char* kCropCategoryNames[] = {
    "Grain", "Root", "Fiber", "Herb", "Fruit", "Magic",
};

// Season identifier. Matches SeasonSystem::Season enum.
// 0=Spring, 1=Summer, 2=Autumn, 3=Winter, -1=Any
using CropSeason = int;

struct CropSpeciesDef {
    // Unique species key, e.g. "wheat", "carrot", "cotton".
    std::string species_key;
    // Translation key, e.g. "crop.wheat".
    std::string title_key;

    // --- Category ---
    CropCategory category = CropCategory::GRAIN;

    // --- Biome constraints (matches TreeSpeciesDef convention) ---
    float temperature_min = -1.0f;
    float temperature_max = 1.0f;
    float humidity_min = -1.0f;
    float humidity_max = 1.0f;

    // --- Season constraints ---
    // -1 means any season.
    CropSeason plant_season = -1;    // Suitable planting season
    CropSeason grow_season = -1;     // Suitable growing season (off-season rate x0.3)
    CropSeason harvest_season = -1;  // Suitable harvest season

    // --- Growth (ticks per stage transition, modulated by ecosystem) ---
    int64_t ticks_seed_to_sprout = 3000;
    int64_t ticks_sprout_to_growing = 6000;
    int64_t ticks_growing_to_mature = 9000;

    // --- Item production ---
    // Seed item key (consumed when planting), e.g. "seed.wheat".
    std::string seed_item_key;
    // Harvest main product item key, e.g. "crop.wheat".
    std::string crop_item_key;
    // Harvest byproduct item key (optional, e.g. wheat seeds). Empty = none.
    std::string byproduct_item_key;
    // Main product min/max count.
    int crop_min = 1;
    int crop_max = 2;
    // Byproduct count (fixed).
    int byproduct_count = 1;
    // Whether the crop can be harvested multiple times (e.g. berry bush).
    // false = crop disappears after harvest.
    bool repeat_harvest = false;
    // Regrow ticks for repeat-harvest crops (after harvest, regrows to mature).
    int64_t regrow_ticks = 6000;

    // --- Terrain materials ---
    // Terrain material keys for each of the 4 growth stages.
    // e.g. wheat: ["snt:wheat_seed", "snt:wheat_sprout",
    //              "snt:wheat_growing", "snt:wheat_mature"]
    std::string stage_material_keys[4];

    // --- Fertility/water sensitivity ---
    // 0.0 = insensitive (weed-level), 1.0 = very sensitive (barren = no yield).
    float fertility_sensitivity = 0.7f;
    float water_sensitivity = 0.7f;

    // --- Wild generation (Tier 1 wild foraging) ---
    // Whether this crop is placed as a wild crop during world generation.
    bool wild_spawn = false;
    // Wild generation density weight (relative to other wild crops).
    float wild_density_weight = 1.0f;

    // --- Visual color (icon/render fallback) ---
    float crop_color_r = 0.85f;
    float crop_color_g = 0.75f;
    float crop_color_b = 0.25f;
};

} // namespace snt::data
