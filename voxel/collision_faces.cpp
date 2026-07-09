// Collision face builder implementation.
//
// Ported from src/bindings/world/gd_chunk_helper.cpp (build_collision_faces).
// Algorithm preserved 1:1; only I/O types changed.

#include "voxel/collision_faces.h"

#include <cstdint>

namespace snt::voxel {

namespace {

// Terrain index: (y * size_z + z) * size_x + x. Matches TerrainData::index_of.
inline int64_t terrain_index(int32_t x, int32_t y, int32_t z,
                             int32_t size_x, int32_t size_z) {
    return static_cast<int64_t>((y * size_z + z) * size_x + x);
}

// Face direction constants: 0=+Y(top), 1=-Y(bottom), 2=+X, 3=-X, 4=+Z, 5=-Z.
struct FaceDir {
    static constexpr int kTop    = 0;
    static constexpr int kBottom = 1;
    static constexpr int kPosX   = 2;
    static constexpr int kNegX   = 3;
    static constexpr int kPosZ   = 4;
    static constexpr int kNegZ   = 5;
    static constexpr int kCount  = 6;
};

inline bool is_collidable_material(
        int32_t material, const std::vector<uint8_t>& mask) {
    return material >= 0 && material < static_cast<int32_t>(mask.size())
        && mask[material] != 0;
}

// Returns true if the machine overlay marks this cell as occupied.
inline bool is_machine_collision_cell(
        int32_t x, int32_t y, int32_t z,
        const std::vector<uint8_t>& machine_mask,
        int32_t size_x, int32_t size_z) {
    if (machine_mask.empty()) return false;
    const int64_t idx = terrain_index(x, y, z, size_x, size_z);
    if (idx < 0 || idx >= static_cast<int64_t>(machine_mask.size())) {
        return false;
    }
    return machine_mask[idx] != 0;
}

// A cell is collidable if its material is collidable OR the machine
// overlay marks it.
bool is_collidable_cell(
        int32_t x, int32_t y, int32_t z,
        const std::vector<uint8_t>& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        const std::vector<uint8_t>& collidable_mask,
        const std::vector<uint8_t>& machine_mask) {
    const int64_t idx = terrain_index(x, y, z, size_x, size_z);
    if (idx < 0 || idx >= static_cast<int64_t>(materials.size())) {
        return is_machine_collision_cell(x, y, z, machine_mask, size_x, size_z);
    }
    const int32_t mat = static_cast<int32_t>(materials[idx]);
    return is_collidable_material(mat, collidable_mask)
        || is_machine_collision_cell(x, y, z, machine_mask, size_x, size_z);
}

// A neighbor cell is "open" (emits a face) when it is out of range or
// not collidable.
bool is_open_for_collision(
        int32_t x, int32_t y, int32_t z,
        const std::vector<uint8_t>& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        const std::vector<uint8_t>& collidable_mask,
        const std::vector<uint8_t>& machine_mask) {
    if (x < 0 || x >= size_x || y < 0 || y >= size_y || z < 0 || z >= size_z) {
        return true;
    }
    return !is_collidable_cell(x, y, z, materials, size_x, size_y, size_z,
                               collidable_mask, machine_mask);
}

}  // namespace

CollisionMeshData build_collision_faces(
        const std::vector<uint8_t>& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        const CollisionMeshOptions& options) {
    CollisionMeshData mesh;

    for (int32_t y = 0; y < size_y; ++y) {
        for (int32_t z = 0; z < size_z; ++z) {
            for (int32_t x = 0; x < size_x; ++x) {
                const int64_t idx = terrain_index(x, y, z, size_x, size_z);
                if (idx < 0 || idx >= static_cast<int64_t>(materials.size())) {
                    continue;
                }
                const int32_t mat = static_cast<int32_t>(materials[idx]);
                const bool material_collidable = is_collidable_material(
                    mat, options.collidable_material_mask);
                const bool machine_collidable = is_machine_collision_cell(
                    x, y, z, options.machine_collision_mask, size_x, size_z);
                if (!material_collidable && !machine_collidable) {
                    continue;
                }

                // 6 neighbor offsets, matching FaceDir order.
                static const int kOffsets[6][3] = {
                    { 0,  1,  0}, { 0, -1,  0},
                    { 1,  0,  0}, {-1,  0,  0},
                    { 0,  0,  1}, { 0,  0, -1},
                };

                for (int dir = 0; dir < FaceDir::kCount; ++dir) {
                    const int nx = x + kOffsets[dir][0];
                    const int ny = y + kOffsets[dir][1];
                    const int nz = z + kOffsets[dir][2];
                    if (!is_open_for_collision(
                            nx, ny, nz, materials, size_x, size_y, size_z,
                            options.collidable_material_mask,
                            options.machine_collision_mask)) {
                        continue;
                    }

                    const float fx = static_cast<float>(x);
                    const float fy = static_cast<float>(y);
                    const float fz = static_cast<float>(z);

                    float v0[3], v1[3], v2[3], v3[3];
                    switch (dir) {
                        case FaceDir::kTop:
                            v0[0] = fx;     v0[1] = fy + 1; v0[2] = fz;
                            v1[0] = fx + 1; v1[1] = fy + 1; v1[2] = fz;
                            v2[0] = fx + 1; v2[1] = fy + 1; v2[2] = fz + 1;
                            v3[0] = fx;     v3[1] = fy + 1; v3[2] = fz + 1;
                            break;
                        case FaceDir::kBottom:
                            v0[0] = fx;     v0[1] = fy;     v0[2] = fz + 1;
                            v1[0] = fx + 1; v1[1] = fy;     v1[2] = fz + 1;
                            v2[0] = fx + 1; v2[1] = fy;     v2[2] = fz;
                            v3[0] = fx;     v3[1] = fy;     v3[2] = fz;
                            break;
                        case FaceDir::kPosX:
                            v0[0] = fx + 1; v0[1] = fy;     v0[2] = fz + 1;
                            v1[0] = fx + 1; v1[1] = fy + 1; v1[2] = fz + 1;
                            v2[0] = fx + 1; v2[1] = fy + 1; v2[2] = fz;
                            v3[0] = fx + 1; v3[1] = fy;     v3[2] = fz;
                            break;
                        case FaceDir::kNegX:
                            v0[0] = fx;     v0[1] = fy;     v0[2] = fz;
                            v1[0] = fx;     v1[1] = fy + 1; v1[2] = fz;
                            v2[0] = fx;     v2[1] = fy + 1; v2[2] = fz + 1;
                            v3[0] = fx;     v3[1] = fy;     v3[2] = fz + 1;
                            break;
                        case FaceDir::kPosZ:
                            v0[0] = fx;     v0[1] = fy;     v0[2] = fz + 1;
                            v1[0] = fx;     v1[1] = fy + 1; v1[2] = fz + 1;
                            v2[0] = fx + 1; v2[1] = fy + 1; v2[2] = fz + 1;
                            v3[0] = fx + 1; v3[1] = fy;     v3[2] = fz + 1;
                            break;
                        case FaceDir::kNegZ:
                            v0[0] = fx + 1; v0[1] = fy;     v0[2] = fz;
                            v1[0] = fx + 1; v1[1] = fy + 1; v1[2] = fz;
                            v2[0] = fx;     v2[1] = fy + 1; v2[2] = fz;
                            v3[0] = fx;     v3[1] = fy;     v3[2] = fz;
                            break;
                        default:
                            continue;
                    }

                    const uint32_t base = static_cast<uint32_t>(
                        mesh.vertices.size() / 3);
                    mesh.vertices.push_back(v0[0]); mesh.vertices.push_back(v0[1]); mesh.vertices.push_back(v0[2]);
                    mesh.vertices.push_back(v1[0]); mesh.vertices.push_back(v1[1]); mesh.vertices.push_back(v1[2]);
                    mesh.vertices.push_back(v2[0]); mesh.vertices.push_back(v2[1]); mesh.vertices.push_back(v2[2]);
                    mesh.vertices.push_back(v3[0]); mesh.vertices.push_back(v3[1]); mesh.vertices.push_back(v3[2]);
                    mesh.indices.push_back(base);
                    mesh.indices.push_back(base + 1);
                    mesh.indices.push_back(base + 2);
                    mesh.indices.push_back(base);
                    mesh.indices.push_back(base + 2);
                    mesh.indices.push_back(base + 3);
                }
            }
        }
    }

    return mesh;
}

}  // namespace snt::voxel
