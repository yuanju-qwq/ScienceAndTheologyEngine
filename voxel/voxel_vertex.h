// Voxel vertex format + mesh data containers.
//
// VoxelVertex is the GPU vertex layout for chunk meshes. It is forward-
// looking: material_id + face_type let P4 add an atlas texture and look
// up UVs per-face in the shader, without changing the vertex format or
// pipeline. For P3 verification the fragment shader hashes material_id
// into a solid color so we can see chunk geometry without textures.
//
// Layering: pure data header. No Vulkan dependency. The matching vertex
// input description lives in vulkan_pipeline.cpp (voxel pipeline variant).

#pragma once

#include <cstdint>
#include <vector>

namespace snt::voxel {

// Vertex layout for chunk meshes. Matches voxel.vert inputs:
//   location 0: position  (vec3)
//   location 1: normal    (vec3)
//   location 2: material_id (uint)
//   location 3: face_type (float)  0=top, 1=bottom, 2=sides
//
// Kept as plain floats/uint for stable memcpy into the vertex buffer.
// 3*4 + 3*4 + 4 + 4 = 32 bytes per vertex.
struct VoxelVertex {
    float    position[3];
    float    normal[3];
    uint32_t material_id;
    float    face_type;
};

// Single-material mesh slice produced by greedy meshing. The chunk
// renderer merges all materials into one vertex/index buffer (material_id
// is per-vertex), but the mesher still groups by material internally so
// the algorithm can be reused for per-material draws if needed.
struct VoxelMeshData {
    std::vector<VoxelVertex> vertices;
    std::vector<uint32_t>    indices;
};

// Collision mesh: position-only triangles fed to the physics layer.
// Kept separate from the render mesh because collision needs the full
// block faces (no greedy merge) and never needs normals/materials.
struct CollisionMeshData {
    std::vector<float>    vertices;   // flat (x,y,z) triples
    std::vector<uint32_t> indices;
};

}  // namespace snt::voxel
