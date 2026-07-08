// TreeSpeciesDef — defines a tree species for world generation.
//
// Ported from src/core/world_gen/tree_species_def.hpp.
// Namespace: science_and_theology -> snt::data.

#pragma once

#include <cstdint>
#include <string>

namespace snt::data {

// ============================================================
// TreeSpeciesDef — defines a tree species for world generation
// ============================================================
//
// Each tree species has unique properties controlling:
//   - Where it grows (biome constraints via temperature/humidity)
//   - How it looks (trunk height, canopy shape, canopy radius)
//   - What it produces (wood material, leaves material, fruit item)
//   - How it grows (growth ticks per stage, whether it's evergreen)
//
// Species are registered in GDScript (BuiltinTerrainContent.gd) and
// consumed by the C++ terrain generator and TreeGrowthSystem.

// Canopy shape determines how pass_surface_objects places leaf blocks.
enum class CanopyShape : uint8_t {
    SPHERE      = 0,   // Round canopy (oak, maple)
    CONE        = 1,   // Triangular canopy (spruce, sequoia)
    UMBRELLA    = 2,   // Flat-topped wide canopy (acacia)
    COLUMN      = 3,   // Tall narrow canopy (birch)
    COUNT       = 4,
};

constexpr const char* kCanopyShapeNames[] = {
    "Sphere", "Cone", "Umbrella", "Column",
};

struct TreeSpeciesDef {
    // Unique species key, e.g. "oak", "birch", "spruce".
    std::string species_key;

    // Translation key, e.g. "tree.oak", "tree.birch".
    std::string title_key;

    // --- Biome constraints ---

    // Temperature range for this species (matches biome noise output).
    // Typical range: -1.0 (cold) to +1.0 (hot).
    float temperature_min = -1.0f;
    float temperature_max = 1.0f;

    // Humidity range for this species (matches biome noise output).
    // Typical range: -1.0 (dry) to +1.0 (wet).
    float humidity_min = -1.0f;
    float humidity_max = 1.0f;

    // Density weight: controls how frequently this species appears
    // relative to other species in the same biome.
    // 1.0 = normal, 0.5 = half as frequent, 2.0 = twice as frequent.
    float density_weight = 1.0f;

    // --- Tree shape ---

    // Trunk height range (in blocks).
    int min_trunk_height = 3;
    int max_trunk_height = 5;

    // Canopy shape.
    CanopyShape canopy_shape = CanopyShape::SPHERE;

    // Canopy radius (in blocks, from trunk center).
    int canopy_radius = 2;

    // --- Material references ---

    // Terrain material key for this species' wood (trunk).
    // e.g. "snt:oak_wood", "snt:birch_wood".
    std::string wood_material_key;

    // Terrain material key for this species' leaves.
    // e.g. "snt:oak_leaves", "snt:birch_leaves".
    std::string leaves_material_key;

    // Terrain material key for this species' sapling.
    // e.g. "snt:oak_sapling", "snt:birch_sapling".
    std::string sapling_material_key;

    // --- Growth ---

    // Whether this tree is evergreen (keeps leaves in winter).
    bool is_evergreen = false;

    // Ticks required for each growth transition.
    // SAPLING -> YOUNG
    int64_t ticks_to_young = 24000;   // ~20 minutes at 20 TPS
    // YOUNG -> MATURE
    int64_t ticks_to_mature = 48000;  // ~40 minutes at 20 TPS

    // --- Fruit (optional) ---

    // Whether this tree produces fruit.
    bool has_fruit = false;

    // Item key for the fruit drop, e.g. "fruit.apple", "fruit.cherry".
    std::string fruit_item_key;

    // Season when fruit drops (0=Spring, 1=Summer, 2=Autumn, 3=Winter).
    // -1 means fruit drops in any season when mature.
    int fruit_season = -1;

    // --- Visual ---

    // Wood color for item icons and plank rendering (RGB 0-1).
    float wood_color_r = 0.55f;
    float wood_color_g = 0.35f;
    float wood_color_b = 0.15f;

    // Leaves color for seasonal tinting (base summer color).
    float leaves_color_r = 0.2f;
    float leaves_color_g = 0.6f;
    float leaves_color_b = 0.1f;

    // Autumn color (for deciduous trees).
    float autumn_color_r = 0.8f;
    float autumn_color_g = 0.5f;
    float autumn_color_b = 0.1f;
};

} // namespace snt::data
