// Vulkan Mesh — vertex buffer + index buffer loaded from .obj file.
//
// P1.5: loads a .obj file via tinyobjloader, creates VMA-backed vertex +
// index buffers, and provides draw() to bind + draw.
//
// P2+ will add: mesh caching, async loading via JobSystem, instanced rendering.

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace snt::render_backend {

class VulkanDevice;
class VulkanBuffer;

// Vertex layout: position (3D) + color (3D), interleaved.
// Matches the mesh.vert shader input.
struct MeshVertex {
    float position[3];
    float color[3];
};

class VulkanMesh {
public:
    VulkanMesh() = default;
    ~VulkanMesh();

    VulkanMesh(const VulkanMesh&) = delete;
    VulkanMesh& operator=(const VulkanMesh&) = delete;

    // Load a .obj file and create vertex + index buffers.
    // `default_color` is applied to all vertices (P1.5: no material support).
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> load_obj(VulkanDevice& device, const std::string& path,
                                       const float default_color[3]);

    void destroy();

    // Bind vertex + index buffers and issue draw call.
    void draw(VkCommandBuffer cmd);

    uint32_t vertex_count() const { return vertex_count_; }
    uint32_t index_count() const { return index_count_; }

private:
    VulkanDevice* device_ = nullptr;
    VulkanBuffer* vertex_buffer_ = nullptr;
    VulkanBuffer* index_buffer_ = nullptr;
    uint32_t vertex_count_ = 0;
    uint32_t index_count_ = 0;
};

}  // namespace snt::render_backend
