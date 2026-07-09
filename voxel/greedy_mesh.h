// Greedy mesher for voxel chunks.
//
// Ported from src/bindings/world/gd_chunk_helper.cpp (build_greedy_mesh).
// Namespace: science_and_theology -> snt::voxel.
// Algorithm preserved 1:1; only I/O types changed (Godot Dictionary ->
// plain C++ structs, PackedByteArray -> std::vector<uint8_t>).
//
// The greedy algorithm merges adjacent same-material faces along the
// secondary axis, producing wider/taller quads instead of one quad per
// voxel face. This drastically reduces vertex/triangle count.
//
// Layering: pure algorithm. Depends only on voxel_vertex.h. No Vulkan,
// no ECS, no Godot. Safe to call from worker threads.

#pragma once

#include <cstdint>
#include <vector>

#include "voxel/voxel_vertex.h"

namespace snt::voxel {

// Per-direction neighbor materials for chunk-boundary face culling.
// Each entry is a full chunk-sized materials array (size_x*size_y*size_z
// bytes) for the neighbor in that direction, or empty if the neighbor
// chunk is not loaded. Index order: 0=+Y, 1=-Y, 2=+X, 3=-X, 4=+Z, 5=-Z.
struct NeighborMaterials {
    std::vector<uint8_t> faces[6];
    bool                 available[6] = {};
};

// Greedy-mesh a chunk's material volume into a render mesh.
//
// `materials` is indexed by (y * size_z + z) * size_x + x (matches
// TerrainData::index_of + GDChunkHelper::terrain_index).
// `transparent_material_mask` is a 256-byte lookup: mask[material] != 0
// means the material is render-transparent (water, ice, glass). Faces
// between two transparent materials emit only once (higher material id
// wins) to avoid z-fighting double-walls.
// `neighbor_materials` provides adjacent chunks' materials so faces at
// chunk boundaries are culled against loaded neighbors. Missing
// neighbors keep their boundary faces (except transparent walls).
//
// Output: a single merged VoxelMeshData with material_id baked per
// vertex. The chunk renderer uploads this as one vertex/index buffer
// and draws it in a single draw call.
VoxelMeshData build_greedy_mesh(
    const std::vector<uint8_t>& materials,
    int32_t size_x, int32_t size_y, int32_t size_z,
    int32_t air_material, int32_t ladder_material,
    const std::vector<uint8_t>& transparent_material_mask,
    const NeighborMaterials&    neighbor_materials);

}  // namespace snt::voxel
