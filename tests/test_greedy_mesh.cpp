#include "voxel/greedy_mesh.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

float triangle_dot_normal(const snt::voxel::VoxelMeshData& mesh, size_t tri_index) {
    const uint32_t i0 = mesh.indices[tri_index + 0];
    const uint32_t i1 = mesh.indices[tri_index + 1];
    const uint32_t i2 = mesh.indices[tri_index + 2];

    const auto& v0 = mesh.vertices[i0];
    const auto& v1 = mesh.vertices[i1];
    const auto& v2 = mesh.vertices[i2];

    const float ax = v1.position[0] - v0.position[0];
    const float ay = v1.position[1] - v0.position[1];
    const float az = v1.position[2] - v0.position[2];
    const float bx = v2.position[0] - v0.position[0];
    const float by = v2.position[1] - v0.position[1];
    const float bz = v2.position[2] - v0.position[2];

    const float cx = ay * bz - az * by;
    const float cy = az * bx - ax * bz;
    const float cz = ax * by - ay * bx;

    return cx * v0.normal[0] + cy * v0.normal[1] + cz * v0.normal[2];
}

}  // namespace

TEST(GreedyMeshTest, SingleSolidVoxelUsesOutwardWinding) {
    const std::vector<uint8_t> materials = {1};
    const std::vector<uint8_t> transparent_mask;
    const snt::voxel::NeighborMaterials neighbors;

    const snt::voxel::VoxelMeshData mesh = snt::voxel::build_greedy_mesh(
        materials,
        1, 1, 1,
        0, 255,
        transparent_mask,
        neighbors);

    ASSERT_EQ(mesh.vertices.size(), 24u);
    ASSERT_EQ(mesh.indices.size(), 36u);

    for (size_t tri = 0; tri < mesh.indices.size(); tri += 3) {
        EXPECT_GT(triangle_dot_normal(mesh, tri), 0.0f)
            << "triangle " << (tri / 3) << " has inward winding";
    }
}
