// GregTech-style voltage tiers and cable materials.
//
// Ported from src/core/config/gt_values.hpp.
// Namespace: science_and_theology::gt -> snt::data (gt sub-namespace merged
// into snt::data per P2 decision).

#pragma once

#include <cstdint>
#include <limits>

namespace snt::data {

// Voltage tier enumeration.
// Mirrors the GregTech 16-tier voltage progression.
enum class VoltageTier : uint8_t {
    ULV = 0,
    LV,
    MV,
    HV,
    EV,
    IV,
    LuV,
    ZPM,
    UV,
    UHV,
    UEV,
    UIV,
    UMV,
    UXV,
    MAX,
    COUNT
};

// Nominal voltage per tier (EU/t equivalent).
constexpr int64_t kVoltageValues[] = {
    8,          // ULV
    32,         // LV
    128,        // MV
    512,        // HV
    2'048,      // EV
    8'192,      // IV
    32'768,     // LuV
    131'072,    // ZPM
    524'288,    // UV
    2'097'152,  // UHV
    8'388'608,  // UEV
    33'554'432, // UIV
    134'217'728,// UMV
    536'870'912,// UXV
    std::numeric_limits<int64_t>::max() - 7, // MAX
};

// Display name for each voltage tier.
constexpr const char* kVoltageNames[] = {
    "ULV", "LV", "MV", "HV", "EV",
    "IV", "LuV", "ZPM", "UV", "UHV",
    "UEV", "UIV", "UMV", "UXV", "MAX"
};

// Returns the nominal voltage for a given tier.
inline constexpr int64_t get_voltage(VoltageTier tier) {
    return kVoltageValues[static_cast<uint8_t>(tier)];
}

// Returns the short display name for a given tier.
inline constexpr const char* get_voltage_name(VoltageTier tier) {
    return kVoltageNames[static_cast<uint8_t>(tier)];
}

// Cable material properties.
// Different materials have different amperage ratings and loss characteristics
// at the same voltage tier. See kCableMaterials for the built-in table.
struct CableProperties {
    const char* material_name = "";
    VoltageTier max_voltage_tier = VoltageTier::ULV;
    int64_t amperage = 1;         // base amperage (1A, 2A, 4A, 8A, 16A)
    int64_t loss_per_tile = 1;    // power units lost per tile of distance
};

// Cable materials table. Covers ULV through UV with representative materials.
// Each material entry: {name, tier, amperage, loss_per_tile}
constexpr CableProperties kCableMaterials[] = {
    // ULV tier
    {"Tin",              VoltageTier::ULV, 1, 1},
    {"Red Alloy",        VoltageTier::ULV, 2, 2},

    // LV tier
    {"Lead",             VoltageTier::LV, 1, 2},
    {"Copper",           VoltageTier::LV, 1, 1},
    {"Tin Alloy",        VoltageTier::LV, 2, 1},

    // MV tier
    {"Annealed Copper",  VoltageTier::MV, 1, 1},
    {"Gold",             VoltageTier::MV, 2, 2},

    // HV tier
    {"Silver",           VoltageTier::HV, 1, 1},
    {"Electrum",         VoltageTier::HV, 2, 2},

    // EV tier
    {"Aluminium",        VoltageTier::EV, 2, 2},
    {"Platinum",         VoltageTier::EV, 4, 1},

    // IV tier
    {"Tungsten",         VoltageTier::IV, 2, 2},
    {"Tungstensteel",    VoltageTier::IV, 4, 1},

    // LuV tier
    {"Graphene",         VoltageTier::LuV, 4, 1},
    {"HSS-G",            VoltageTier::LuV, 8, 2},

    // ZPM tier
    {"Naquadah",         VoltageTier::ZPM, 4, 1},
    {"Naquadah Alloy",   VoltageTier::ZPM, 8, 1},

    // UV tier
    {"Superconductor",   VoltageTier::UV, 8, 0},
    {"Superconductor HV", VoltageTier::UV, 16, 0},

    // UHV+ tier cable materials (TBD — add as progression demands).
};

inline constexpr size_t kCableMaterialCount =
    sizeof(kCableMaterials) / sizeof(kCableMaterials[0]);

// Returns the full power capacity of a cable: voltage x amperage.
// For example, Copper (LV=32V, 1A) -> capacity 32.
inline constexpr int64_t get_cable_capacity(const CableProperties& cable) {
    return get_voltage(cable.max_voltage_tier) * cable.amperage;
}

// Computes power loss over a given distance (in tiles) for a cable material.
// For example, Copper (loss_per_tile=1) over 5 tiles -> loss 5.
inline constexpr int64_t get_cable_loss(const CableProperties& cable,
                                         int64_t distance_tiles) {
    return cable.loss_per_tile * distance_tiles;
}

// Computes the Manhattan distance between two 3D map positions.
inline constexpr int64_t manhattan_distance(int32_t x1, int32_t y1, int32_t z1,
                                             int32_t x2, int32_t y2, int32_t z2) {
    int64_t dx = (x1 > x2) ? static_cast<int64_t>(x1 - x2)
                           : static_cast<int64_t>(x2 - x1);
    int64_t dy = (y1 > y2) ? static_cast<int64_t>(y1 - y2)
                           : static_cast<int64_t>(y2 - y1);
    int64_t dz = (z1 > z2) ? static_cast<int64_t>(z1 - z2)
                           : static_cast<int64_t>(z2 - z1);
    return dx + dy + dz;
}

// Long descriptive name for each voltage tier.
constexpr const char* kVoltageLongNames[] = {
    "Ultra Low Voltage",
    "Low Voltage",
    "Medium Voltage",
    "High Voltage",
    "Extreme Voltage",
    "Insane Voltage",
    "Ludicrous Voltage",
    "ZPM Voltage",
    "Ultimate Voltage",
    "Ultimate High Voltage",
    "Ultimate Extreme Voltage",
    "Ultimate Insane Voltage",
    "Ultimate Mega Voltage",
    "Ultimate Extended Mega Voltage",
    "Maximum Voltage",
};

// Returns the index of a cable material by name, or kCableMaterialCount if not found.
inline constexpr size_t find_cable_material(const char* name) {
    for (size_t i = 0; i < kCableMaterialCount; ++i) {
        // Simple pointer comparison for constexpr string literals.
        if (kCableMaterials[i].material_name == name) {
            return i;
        }
    }
    return kCableMaterialCount;
}

// Returns the highest tier whose nominal voltage does not exceed the given value.
inline constexpr VoltageTier tier_from_voltage(int64_t voltage) {
    for (int i = static_cast<int>(VoltageTier::COUNT) - 1; i >= 0; --i) {
        if (kVoltageValues[i] <= voltage) {
            return static_cast<VoltageTier>(i);
        }
    }
    return VoltageTier::ULV;
}

} // namespace snt::data
