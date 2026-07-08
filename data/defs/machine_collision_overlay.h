// MachineCollisionOverlay — sparse overlay of machine-occupied cells.
//
// Ported from src/core/world/machine_collision_overlay.hpp.
// Namespace: science_and_theology -> snt::data.
//
// Machines (furnaces, campfires, magic structures, ...) contribute collision
// through this overlay instead of per-object physics nodes.
// The overlay is consumed by collision mesh generation, which merges
// machine-occupied cells into the per-chunk collision mesh.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace snt::data {

// Identifies a single voxel cell occupied by a machine collision shape.
struct MachineCellKey {
    std::string dimension_id;
    int32_t cell_x = 0;
    int32_t cell_y = 0;
    int32_t cell_z = 0;

    MachineCellKey() = default;

    MachineCellKey(std::string dimension, int32_t x, int32_t y, int32_t z)
        : dimension_id(std::move(dimension))
        , cell_x(x)
        , cell_y(y)
        , cell_z(z) {
    }

    bool operator==(const MachineCellKey& other) const {
        return cell_x == other.cell_x
            && cell_y == other.cell_y
            && cell_z == other.cell_z
            && dimension_id == other.dimension_id;
    }
};

struct MachineCellKeyHash {
    size_t operator()(const MachineCellKey& key) const {
        size_t h = std::hash<std::string>()(key.dimension_id);
        h ^= std::hash<int32_t>()(key.cell_x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>()(key.cell_y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>()(key.cell_z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// Sparse overlay of machine-occupied cells per (dimension, cell) coordinate.
// Lives inside WorldData so chunk collision generation can consult it without
// going through Godot signals. Pure C++ — no Godot types — so it can be
// exercised by headless unit tests and a future dedicated server.
//
// Lifecycle:
//   - set(..., true) when a machine is placed at a cell.
//   - set(..., false) when the machine is removed.
//   - Query get_chunk_mask(...) when building a chunk's collision mesh.
//   - clear_dimension(...) when a planet is unloaded.
//   - State is NOT persisted here; on load the owning manager (e.g.
//     FurnaceManager) re-applies the overlay from its own save data.
class MachineCollisionOverlay {
public:
    MachineCollisionOverlay() = default;
    ~MachineCollisionOverlay() = default;

    MachineCollisionOverlay(const MachineCollisionOverlay&) = delete;
    MachineCollisionOverlay& operator=(const MachineCollisionOverlay&) = delete;
    MachineCollisionOverlay(MachineCollisionOverlay&&) = default;
    MachineCollisionOverlay& operator=(MachineCollisionOverlay&&) = default;

    // Mark or unmark a single cell as machine-occupied.
    void set(const std::string& dimension_id,
             int32_t cell_x, int32_t cell_y, int32_t cell_z,
             bool occupied);

    // Convenience: marks occupied.
    void mark(const std::string& dimension_id,
              int32_t cell_x, int32_t cell_y, int32_t cell_z) {
        set(dimension_id, cell_x, cell_y, cell_z, true);
    }

    // Convenience: clears occupied.
    void clear(const std::string& dimension_id,
               int32_t cell_x, int32_t cell_y, int32_t cell_z) {
        set(dimension_id, cell_x, cell_y, cell_z, false);
    }

    // Returns true if a cell is marked as machine-occupied.
    bool is_occupied(const std::string& dimension_id,
                     int32_t cell_x, int32_t cell_y, int32_t cell_z) const;

    // Removes all entries for a dimension. Returns the number of entries removed.
    size_t clear_dimension(const std::string& dimension_id);

    // Removes all entries across all dimensions.
    void clear_all();

    // Total number of marked cells across all dimensions.
    size_t size() const { return cells_.size(); }

    // Build a dense per-cell mask for a chunk: 1 if the cell is machine-occupied,
    // 0 otherwise. The result has size_x * size_y * size_z entries indexed by
    // terrain_index(local_x, local_y, local_z, size_x, size_z). Cells outside
    // the overlay default to 0. Returned vector is resized (not reserved) so
    // callers can index directly without further setup.
    std::vector<uint8_t> get_chunk_mask(const std::string& dimension_id,
                                        int32_t chunk_x, int32_t chunk_y,
                                        int32_t chunk_z,
                                        int32_t size_x, int32_t size_y,
                                        int32_t size_z) const;

private:
    std::unordered_set<MachineCellKey, MachineCellKeyHash> cells_;
};

} // namespace snt::data
