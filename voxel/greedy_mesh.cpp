// Greedy mesher implementation.
//
// Ported from src/bindings/world/gd_chunk_helper.cpp (build_greedy_mesh).
// Algorithm preserved 1:1; only I/O types changed.

#include "voxel/greedy_mesh.h"

#include <array>
#include <unordered_map>
#include <vector>

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

// Map face direction to face type for UV selection: 0=top, 1=bottom, 2=sides.
inline int face_type(int dir) {
    if (dir == FaceDir::kTop)    return 0;
    if (dir == FaceDir::kBottom) return 1;
    return 2;
}

// Normal vector for each face direction.
inline void face_normal(int dir, float out[3]) {
    switch (dir) {
        case FaceDir::kTop:    out[0] = 0;  out[1] =  1; out[2] = 0; break;
        case FaceDir::kBottom: out[0] = 0;  out[1] = -1; out[2] = 0; break;
        case FaceDir::kPosX:   out[0] =  1; out[1] = 0; out[2] = 0; break;
        case FaceDir::kNegX:   out[0] = -1; out[1] = 0; out[2] = 0; break;
        case FaceDir::kPosZ:   out[0] = 0;  out[1] = 0; out[2] =  1; break;
        case FaceDir::kNegZ:   out[0] = 0;  out[1] = 0; out[2] = -1; break;
        default:               out[0] = 0;  out[1] =  1; out[2] = 0; break;
    }
}

// Neighbor offset for each face direction.
inline void neighbor_offset(int dir, int out[3]) {
    switch (dir) {
        case FaceDir::kTop:    out[0] = 0;  out[1] =  1; out[2] = 0; break;
        case FaceDir::kBottom: out[0] = 0;  out[1] = -1; out[2] = 0; break;
        case FaceDir::kPosX:   out[0] =  1; out[1] = 0; out[2] = 0; break;
        case FaceDir::kNegX:   out[0] = -1; out[1] = 0; out[2] = 0; break;
        case FaceDir::kPosZ:   out[0] = 0;  out[1] = 0; out[2] =  1; break;
        case FaceDir::kNegZ:   out[0] = 0;  out[1] = 0; out[2] = -1; break;
        default:               out[0] = 0;  out[1] = 0; out[2] = 0; break;
    }
}

// Render-transparent test (water, ice, glass).
inline bool is_render_transparent(int32_t material,
                                  const std::vector<uint8_t>& mask) {
    return material >= 0 && material < static_cast<int32_t>(mask.size())
        && mask[material] != 0;
}

// Sample a neighbor cell's material, handling chunk-boundary wrap via the
// precomputed NeighborMaterials. Returns false if no neighbor data is
// available (caller treats missing neighbor as air/exposed).
bool sample_neighbor_material(
        int32_t x, int32_t y, int32_t z, int dir,
        const std::vector<uint8_t>& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        const NeighborMaterials& neighbors,
        int32_t& material_out) {
    if (x >= 0 && x < size_x && y >= 0 && y < size_y
        && z >= 0 && z < size_z) {
        const int64_t idx = terrain_index(x, y, z, size_x, size_z);
        if (idx < 0 || idx >= static_cast<int64_t>(materials.size())) {
            return false;
        }
        material_out = static_cast<int32_t>(materials[idx]);
        return true;
    }

    if (dir < 0 || dir >= FaceDir::kCount || !neighbors.available[dir]) {
        return false;
    }

    // Wrap the sampled coordinate to the neighbor chunk's local frame.
    switch (dir) {
        case FaceDir::kTop:    y = 0;         break;
        case FaceDir::kBottom: y = size_y - 1; break;
        case FaceDir::kPosX:   x = 0;         break;
        case FaceDir::kNegX:   x = size_x - 1; break;
        case FaceDir::kPosZ:   z = 0;         break;
        case FaceDir::kNegZ:   z = size_z - 1; break;
        default: return false;
    }

    const auto& neighbor = neighbors.faces[dir];
    const int64_t idx = terrain_index(x, y, z, size_x, size_z);
    if (idx < 0 || idx >= static_cast<int64_t>(neighbor.size())) {
        return false;
    }
    material_out = static_cast<int32_t>(neighbor[idx]);
    return true;
}

// Decide whether to emit a face between `material` and `neighbor_material`.
// Mirrors the original rule set exactly:
//   - Missing neighbor + opaque: emit (streaming boundary).
//   - Missing neighbor + transparent: emit only the top face (no side walls).
//   - Air/ladder neighbor: always emit.
//   - Opaque -> opaque: never emit.
//   - Opaque -> transparent: emit (ground visible through water).
//   - Transparent -> same/opaque: skip.
//   - Transparent -> different transparent: emit once (higher material id wins).
bool should_emit_render_face(
        int32_t material, bool has_neighbor, int32_t neighbor_material, int dir,
        int32_t air_material, int32_t ladder_material,
        const std::vector<uint8_t>& transparent_mask) {
    const bool transparent = is_render_transparent(material, transparent_mask);
    if (!has_neighbor) {
        return !transparent || dir == FaceDir::kTop;
    }
    if (neighbor_material == air_material || neighbor_material == ladder_material) {
        return true;
    }
    const bool neighbor_transparent = is_render_transparent(neighbor_material, transparent_mask);
    if (!transparent) {
        return neighbor_transparent;
    }
    if (!neighbor_transparent || neighbor_material == material) {
        return false;
    }
    return material > neighbor_material;
}

// Emit a quad for a merged face. The quad occupies [u_start,u_end] x
// [v_start,v_end] in the face plane, at depth d along the normal axis.
// Vertex winding is preserved from the original so backface culling
// (VK_FRONT_FACE_COUNTER_CLOCKWISE) stays consistent.
void emit_face_quad(VoxelMeshData& mesh, int dir, int32_t d,
                    int32_t u_start, int32_t u_end,
                    int32_t v_start, int32_t v_end,
                    int32_t material_id) {
    const float fd = static_cast<float>(d);
    const float fu0 = static_cast<float>(u_start);
    const float fu1 = static_cast<float>(u_end);
    const float fv0 = static_cast<float>(v_start);
    const float fv1 = static_cast<float>(v_end);

    float nrm[3];
    face_normal(dir, nrm);

    VoxelVertex v0{}, v1{}, v2{}, v3{};
    v0.normal[0] = nrm[0]; v0.normal[1] = nrm[1]; v0.normal[2] = nrm[2];
    v1.normal[0] = nrm[0]; v1.normal[1] = nrm[1]; v1.normal[2] = nrm[2];
    v2.normal[0] = nrm[0]; v2.normal[1] = nrm[1]; v2.normal[2] = nrm[2];
    v3.normal[0] = nrm[0]; v3.normal[1] = nrm[1]; v3.normal[2] = nrm[2];
    const float ft = static_cast<float>(face_type(dir));
    v0.face_type = ft; v1.face_type = ft; v2.face_type = ft; v3.face_type = ft;
    v0.material_id = static_cast<uint32_t>(material_id);
    v1.material_id = static_cast<uint32_t>(material_id);
    v2.material_id = static_cast<uint32_t>(material_id);
    v3.material_id = static_cast<uint32_t>(material_id);

    switch (dir) {
        case FaceDir::kTop:  // +Y, plane: X-Z at y=d+1
            v0.position[0] = fu0; v0.position[1] = fd + 1.0f; v0.position[2] = fv0;
            v1.position[0] = fu1; v1.position[1] = fd + 1.0f; v1.position[2] = fv0;
            v2.position[0] = fu1; v2.position[1] = fd + 1.0f; v2.position[2] = fv1;
            v3.position[0] = fu0; v3.position[1] = fd + 1.0f; v3.position[2] = fv1;
            break;
        case FaceDir::kBottom:  // -Y, plane: X-Z at y=d
            v0.position[0] = fu0; v0.position[1] = fd; v0.position[2] = fv1;
            v1.position[0] = fu1; v1.position[1] = fd; v1.position[2] = fv1;
            v2.position[0] = fu1; v2.position[1] = fd; v2.position[2] = fv0;
            v3.position[0] = fu0; v3.position[1] = fd; v3.position[2] = fv0;
            break;
        case FaceDir::kPosX:  // +X, plane: Z-Y at x=d+1
            v0.position[0] = fd + 1.0f; v0.position[1] = fv0; v0.position[2] = fu1;
            v1.position[0] = fd + 1.0f; v1.position[1] = fv1; v1.position[2] = fu1;
            v2.position[0] = fd + 1.0f; v2.position[1] = fv1; v2.position[2] = fu0;
            v3.position[0] = fd + 1.0f; v3.position[1] = fv0; v3.position[2] = fu0;
            break;
        case FaceDir::kNegX:  // -X, plane: Z-Y at x=d
            v0.position[0] = fd; v0.position[1] = fv0; v0.position[2] = fu0;
            v1.position[0] = fd; v1.position[1] = fv1; v1.position[2] = fu0;
            v2.position[0] = fd; v2.position[1] = fv1; v2.position[2] = fu1;
            v3.position[0] = fd; v3.position[1] = fv0; v3.position[2] = fu1;
            break;
        case FaceDir::kPosZ:  // +Z, plane: X-Y at z=d+1
            v0.position[0] = fu0; v0.position[1] = fv0; v0.position[2] = fd + 1.0f;
            v1.position[0] = fu0; v1.position[1] = fv1; v1.position[2] = fd + 1.0f;
            v2.position[0] = fu1; v2.position[1] = fv1; v2.position[2] = fd + 1.0f;
            v3.position[0] = fu1; v3.position[1] = fv0; v3.position[2] = fd + 1.0f;
            break;
        case FaceDir::kNegZ:  // -Z, plane: X-Y at z=d
            v0.position[0] = fu1; v0.position[1] = fv0; v0.position[2] = fd;
            v1.position[0] = fu1; v1.position[1] = fv1; v1.position[2] = fd;
            v2.position[0] = fu0; v2.position[1] = fv1; v2.position[2] = fd;
            v3.position[0] = fu0; v3.position[1] = fv0; v3.position[2] = fd;
            break;
    }

    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(v0);
    mesh.vertices.push_back(v1);
    mesh.vertices.push_back(v2);
    mesh.vertices.push_back(v3);
    // Two triangles: 0-1-2, 0-2-3.
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

}  // namespace

VoxelMeshData build_greedy_mesh(
        const std::vector<uint8_t>& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        int32_t air_material, int32_t ladder_material,
        const std::vector<uint8_t>& transparent_material_mask,
        const NeighborMaterials&    neighbor_materials) {
    VoxelMeshData mesh;

    // For each of the 6 face directions, sweep the chunk in slices along
    // the normal axis and greedily merge same-material faces.
    for (int dir = 0; dir < FaceDir::kCount; ++dir) {
        // d = depth axis (normal direction), u/v = in-plane axes.
        int32_t d_size, u_size, v_size;
        switch (dir) {
            case FaceDir::kTop:
            case FaceDir::kBottom:
                d_size = size_y; u_size = size_x; v_size = size_z;
                break;
            case FaceDir::kPosX:
            case FaceDir::kNegX:
                d_size = size_x; u_size = size_z; v_size = size_y;
                break;
            case FaceDir::kPosZ:
            case FaceDir::kNegZ:
                d_size = size_z; u_size = size_x; v_size = size_y;
                break;
            default:
                continue;
        }

        for (int32_t d = 0; d < d_size; ++d) {
            // 2D mask: mask[u][v] = material_id if face is exposed, -1 otherwise.
            std::vector<std::vector<int32_t>> mask(
                static_cast<size_t>(u_size),
                std::vector<int32_t>(static_cast<size_t>(v_size), -1));

            for (int32_t u = 0; u < u_size; ++u) {
                for (int32_t v = 0; v < v_size; ++v) {
                    // Convert (d, u, v) back to (x, y, z).
                    int32_t x, y, z;
                    switch (dir) {
                        case FaceDir::kTop:
                        case FaceDir::kBottom:
                            x = u; y = d; z = v; break;
                        case FaceDir::kPosX:
                        case FaceDir::kNegX:
                            x = d; y = v; z = u; break;
                        case FaceDir::kPosZ:
                        case FaceDir::kNegZ:
                            x = u; y = v; z = d; break;
                        default:
                            x = u; y = d; z = v; break;
                    }

                    const int64_t idx = terrain_index(x, y, z, size_x, size_z);
                    if (idx < 0 || idx >= static_cast<int64_t>(materials.size())) {
                        continue;
                    }
                    const int32_t mat = static_cast<int32_t>(materials[idx]);
                    if (mat == air_material || mat == ladder_material) {
                        continue;
                    }

                    // Check if this face is exposed.
                    int off[3];
                    neighbor_offset(dir, off);
                    int32_t nx = x + off[0];
                    int32_t ny = y + off[1];
                    int32_t nz = z + off[2];
                    int32_t neighbor_material = air_material;
                    const bool has_neighbor = sample_neighbor_material(
                        nx, ny, nz, dir, materials,
                        size_x, size_y, size_z,
                        neighbor_materials, neighbor_material);
                    if (!should_emit_render_face(
                            mat, has_neighbor, neighbor_material, dir,
                            air_material, ladder_material,
                            transparent_material_mask)) {
                        continue;
                    }

                    mask[static_cast<size_t>(u)][static_cast<size_t>(v)] = mat;
                }
            }

            // Greedy merge: scan the mask, find rectangles of same material.
            std::vector<std::vector<bool>> visited(
                static_cast<size_t>(u_size),
                std::vector<bool>(static_cast<size_t>(v_size), false));

            for (int32_t u = 0; u < u_size; ++u) {
                for (int32_t v = 0; v < v_size; ++v) {
                    if (visited[static_cast<size_t>(u)][static_cast<size_t>(v)]) continue;
                    const int32_t mat = mask[static_cast<size_t>(u)][static_cast<size_t>(v)];
                    if (mat < 0) continue;

                    // Extend along v (secondary axis) as far as possible.
                    int32_t v_end = v + 1;
                    while (v_end < v_size &&
                           !visited[static_cast<size_t>(u)][static_cast<size_t>(v_end)] &&
                           mask[static_cast<size_t>(u)][static_cast<size_t>(v_end)] == mat) {
                        ++v_end;
                    }

                    // Extend along u (primary axis) as far as possible.
                    int32_t u_end = u + 1;
                    bool can_extend = true;
                    while (can_extend && u_end < u_size) {
                        for (int32_t vv = v; vv < v_end; ++vv) {
                            if (visited[static_cast<size_t>(u_end)][static_cast<size_t>(vv)] ||
                                mask[static_cast<size_t>(u_end)][static_cast<size_t>(vv)] != mat) {
                                can_extend = false;
                                break;
                            }
                        }
                        if (can_extend) ++u_end;
                    }

                    // Mark visited.
                    for (int32_t uu = u; uu < u_end; ++uu) {
                        for (int32_t vv = v; vv < v_end; ++vv) {
                            visited[static_cast<size_t>(uu)][static_cast<size_t>(vv)] = true;
                        }
                    }

                    // Emit the merged quad.
                    emit_face_quad(mesh, dir, d, u, u_end, v, v_end, mat);
                }
            }
        }
    }

    return mesh;
}

}  // namespace snt::voxel
