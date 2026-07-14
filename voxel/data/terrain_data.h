// Generic voxel terrain value types.
//
// This module deliberately carries only cell storage and opaque material
// flags. Material definitions, mining rules, fluids, and ecology are owned by
// the game layer and may interpret these values without leaking into Runtime.

#pragma once

#include <cstdint>
#include <vector>

namespace snt::voxel {

using TerrainMaterialId = uint8_t;
using TerrainMaterial = TerrainMaterialId;
using CellFluidId = uint16_t;

inline constexpr CellFluidId kInvalidCellFluidId = 0xFFFF;
inline constexpr int16_t kCellFluidCapacity = 1000;

enum TerrainFlags : uint32_t {
    TF_WALKABLE = 1 << 0,
    TF_SOLID = 1 << 1,
    TF_LIQUID = 1 << 2,
    TF_MINEABLE = 1 << 3,
    TF_CLIMBABLE = 1 << 4,
    TF_INDESTRUCTIBLE = 1 << 5,
    TF_GRAVITY_FALL = 1 << 6,
    TF_COLLAPSE_RISK = 1 << 7,
    TF_SUPPORT_BEAM = 1 << 8,
};

inline constexpr uint32_t operator|(TerrainFlags left, TerrainFlags right) {
    return static_cast<uint32_t>(left) | static_cast<uint32_t>(right);
}

inline constexpr uint32_t operator|(uint32_t left, TerrainFlags right) {
    return left | static_cast<uint32_t>(right);
}

inline constexpr bool operator&(uint32_t left, TerrainFlags right) {
    return (left & static_cast<uint32_t>(right)) != 0;
}

struct TerrainCell {
    TerrainMaterial material = 0;
    uint32_t flags = 0;
    CellFluidId fluid_type = kInvalidCellFluidId;
    int16_t fluid_mass = 0;
    int16_t fluid_temperature = 300;
    bool fluid_is_gas = false;

    bool is_walkable() const { return flags & TF_WALKABLE; }
    bool is_solid() const { return flags & TF_SOLID; }
    bool is_liquid() const { return flags & TF_LIQUID; }
    bool is_mineable() const { return flags & TF_MINEABLE; }
    bool is_climbable() const { return flags & TF_CLIMBABLE; }
    bool is_indestructible() const { return flags & TF_INDESTRUCTIBLE; }
    bool is_gravity_fall() const { return flags & TF_GRAVITY_FALL; }
    bool is_collapse_risk() const { return flags & TF_COLLAPSE_RISK; }
    bool is_support_beam() const { return flags & TF_SUPPORT_BEAM; }
    bool has_fluid() const { return fluid_mass > 0; }
    bool fluid_is_full() const { return fluid_mass >= kCellFluidCapacity; }

    int16_t fluid_remaining_space() const {
        return fluid_mass < kCellFluidCapacity
            ? static_cast<int16_t>(kCellFluidCapacity - fluid_mass)
            : 0;
    }

    int16_t insert_fluid(CellFluidId fluid, int16_t to_insert, bool is_gas = false) {
        if (to_insert <= 0 ||
            (fluid_type != kInvalidCellFluidId && fluid_type != fluid)) {
            return 0;
        }

        const int16_t inserted = to_insert < fluid_remaining_space()
            ? to_insert
            : fluid_remaining_space();
        fluid_mass += inserted;
        if (fluid_mass > 0 && fluid_type == kInvalidCellFluidId) {
            fluid_type = fluid;
            fluid_is_gas = is_gas;
        }
        return inserted;
    }

    int16_t extract_fluid(int16_t to_extract) {
        if (to_extract <= 0) return 0;
        const int16_t extracted = to_extract < fluid_mass ? to_extract : fluid_mass;
        fluid_mass -= extracted;
        if (fluid_mass <= 0) clear_fluid();
        return extracted;
    }

    void clear_fluid() {
        fluid_type = kInvalidCellFluidId;
        fluid_mass = 0;
        fluid_temperature = 300;
        fluid_is_gas = false;
    }
};

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

}  // namespace snt::voxel
