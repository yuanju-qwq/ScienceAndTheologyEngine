// Collision face builder for voxel chunks.
//
// Ported from src/bindings/world/gd_chunk_helper.cpp (build_collision_faces).
// Namespace: science_and_theology -> snt::voxel.
// Algorithm preserved 1:1; only I/O types changed.
//
// Produces position-only triangles for every exposed face of a collidable
// cell. No greedy merge (physics needs full block faces). Intended to feed
// a single concave collision shape per chunk.
//
// Layering: pure algorithm. No Vulkan, no ECS. Safe from worker threads.

#pragma once

#include <cstdint>
#include <vector>

#include "voxel/voxel_vertex.h"

namespace snt::voxel {

// Per-chunk collision mesh build options.
struct CollisionMeshOptions {
    // 256-byte lookup: mask[material] != 0 means the material is collidable.
    std::vector<uint8_t> collidable_material_mask;

    // Optional per-cell overlay marking cells occupied by machines
    // (furnaces, campfires, ...). Same size as `materials` (size_x*size_y*
    // size_z). Empty disables the overlay. A cell with mask==1 is treated
    // as collidable AND blocks neighbor faces, so machines get collision
    // coverage without per-object physics bodies.
    std::vector<uint8_t> machine_collision_mask;
};

// Build collision faces for a chunk.
//
// `materials` is indexed by (y * size_z + z) * size_x + x.
// Emits 4 vertices + 2 triangles per exposed face of every collidable
// cell (material collidable OR machine overlay marks it).
CollisionMeshData build_collision_faces(
    const std::vector<uint8_t>& materials,
    int32_t size_x, int32_t size_y, int32_t size_z,
    const CollisionMeshOptions& options);

}  // namespace snt::voxel
