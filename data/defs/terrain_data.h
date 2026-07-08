// Terrain voxel data — per-cell material, flags, and fluid state.
//
// Ported from src/core/world/terrain_data.hpp.
// Namespace: science_and_theology -> snt::data.
//
// This is the lowest-level terrain representation: a 3D array of TerrainCell
// where each cell holds a material id, gameplay flags, and optional per-cell
// fluid data. TerrainData wraps the array with helpers for resize/get/set.

#pragma once

#include <cstdint>
#include <vector>

namespace snt::data {

using TerrainMaterialId = uint8_t;
using TerrainMaterial = TerrainMaterialId;

// Fluid type identifier for per-cell fluid data.
// Mirrors FluidId (uint16_t) from resource_types.h to avoid
// cross-namespace dependencies in the world layer.
using CellFluidId = uint16_t;
inline constexpr CellFluidId kInvalidCellFluidId = 0xFFFF;

// Maximum fluid mass per cell in millibuckets (mB).
inline constexpr int16_t kCellFluidCapacity = 1000;

// Per-cell gameplay flags derived from material properties.
enum TerrainFlags : uint32_t {
    TF_WALKABLE       = 1 << 0,
    TF_SOLID          = 1 << 1,
    TF_LIQUID         = 1 << 2,
    TF_MINEABLE       = 1 << 3,
    TF_CLIMBABLE      = 1 << 4,
    TF_INDESTRUCTIBLE = 1 << 5,
    // Gravity-affected: block falls when unsupported (sand, gravel).
    TF_GRAVITY_FALL   = 1 << 6,
    // Collapse-eligible: block can cave-in when structural support is lost (stone, deepstone).
    TF_COLLAPSE_RISK  = 1 << 7,
    // Support beam: provides structural support to nearby blocks, preventing cave-ins.
    TF_SUPPORT_BEAM   = 1 << 8,
};

inline constexpr uint32_t operator|(TerrainFlags a, TerrainFlags b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}
inline constexpr uint32_t operator|(uint32_t a, TerrainFlags b) {
    return a | static_cast<uint32_t>(b);
}
inline constexpr bool operator&(uint32_t a, TerrainFlags b) {
    return (a & static_cast<uint32_t>(b)) != 0;
}

// A single voxel in the terrain volume.
// Contains both solid block data and per-cell fluid data.
// Fluid and solid blocks are mutually exclusive: placing a solid block
// displaces any fluid in the cell (see TileFluidSystem::displace_fluid).
struct TerrainCell {
    TerrainMaterial material = 0;
    uint32_t flags = 0;

    // --- Per-cell fluid data ---
    // Valid when fluid_mass > 0. When a solid block is present,
    // fluid should be displaced before the block is placed.
    CellFluidId fluid_type = kInvalidCellFluidId;
    int16_t fluid_mass = 0;          // 0 ~ kCellFluidCapacity (mB)
    int16_t fluid_temperature = 300; // Kelvin
    bool fluid_is_gas = false;       // true = gas (no gravity, 6-dir diffusion)

    // --- Solid block queries ---
    bool is_walkable() const { return flags & TF_WALKABLE; }
    bool is_solid() const { return flags & TF_SOLID; }
    bool is_liquid() const { return flags & TF_LIQUID; }
    bool is_mineable() const { return flags & TF_MINEABLE; }
    bool is_climbable() const { return flags & TF_CLIMBABLE; }
    bool is_indestructible() const { return flags & TF_INDESTRUCTIBLE; }
    // Gravity-affected: falls when the block below is empty (sand, gravel).
    bool is_gravity_fall() const { return flags & TF_GRAVITY_FALL; }
    // Collapse-eligible: can cave-in when structural support is lost.
    bool is_collapse_risk() const { return flags & TF_COLLAPSE_RISK; }
    // Support beam: prevents cave-ins within its support radius.
    bool is_support_beam() const { return flags & TF_SUPPORT_BEAM; }

    // --- Fluid queries ---
    bool has_fluid() const { return fluid_mass > 0; }
    bool fluid_is_full() const { return fluid_mass >= kCellFluidCapacity; }
    int16_t fluid_remaining_space() const {
        return (fluid_mass < kCellFluidCapacity)
            ? static_cast<int16_t>(kCellFluidCapacity - fluid_mass) : 0;
    }

    // Attempts to insert fluid into this cell.
    // Returns the amount actually inserted.
    int16_t insert_fluid(CellFluidId fluid, int16_t to_insert,
                         bool is_gas = false) {
        if (to_insert <= 0) return 0;
        if (fluid_type != kInvalidCellFluidId && fluid_type != fluid) return 0;
        int16_t space = fluid_remaining_space();
        int16_t inserted = (to_insert < space) ? to_insert : space;
        fluid_mass += inserted;
        if (fluid_mass > 0 && fluid_type == kInvalidCellFluidId) {
            fluid_type = fluid;
            fluid_is_gas = is_gas;
        }
        return inserted;
    }

    // Attempts to extract fluid from this cell.
    // Returns the amount actually extracted.
    int16_t extract_fluid(int16_t to_extract) {
        if (to_extract <= 0) return 0;
        int16_t extracted = (to_extract < fluid_mass) ? to_extract : fluid_mass;
        fluid_mass -= extracted;
        if (fluid_mass <= 0) {
            fluid_type = kInvalidCellFluidId;
            fluid_mass = 0;
        }
        return extracted;
    }

    // Clears all fluid data from this cell.
    void clear_fluid() {
        fluid_type = kInvalidCellFluidId;
        fluid_mass = 0;
        fluid_temperature = 300;
        fluid_is_gas = false;
    }
};

// Terrain data for one chunk. Pure data, no rendering information.
struct TerrainData {
    int size_x = 0;
    int size_y = 0;
    int size_z = 0;
    std::vector<TerrainCell> cells;

    const TerrainCell& cell_at(int x, int y, int z) const {
        return cells[index_of(x, y, z)];
    }

    TerrainCell& cell_at(int x, int y, int z) {
        return cells[index_of(x, y, z)];
    }

    void resize(int x, int y, int z) {
        size_x = x;
        size_y = y;
        size_z = z;
        cells.resize(static_cast<size_t>(x * y * z));
    }

    bool is_valid_cell(int x, int y, int z) const {
        return x >= 0 && x < size_x
            && y >= 0 && y < size_y
            && z >= 0 && z < size_z;
    }

    void set_cell(int x, int y, int z, TerrainMaterial material) {
        set_cell(x, y, z, material, 0);
    }

    void set_cell(int x, int y, int z, TerrainMaterial material, uint32_t flags) {
        if (!is_valid_cell(x, y, z)) return;
        auto& cell = cells[index_of(x, y, z)];
        cell.material = material;
        cell.flags = flags;
    }

    size_t index_of(int x, int y, int z) const {
        return static_cast<size_t>((y * size_z + z) * size_x + x);
    }
};

} // namespace snt::data
