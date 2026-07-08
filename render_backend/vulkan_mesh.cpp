// Vulkan Mesh implementation.

#define SNT_LOG_CHANNEL "render_backend"
#include "core/log.h"

#include "vulkan_mesh.h"
#include "vulkan_buffer.h"
#include "vulkan_device.h"

#include <volk.h>
#include <tiny_obj_loader.h>

#include <cstring>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanMesh::~VulkanMesh() {
    destroy();
}

snt::core::Expected<void> VulkanMesh::load_obj(VulkanDevice& device, const std::string& path,
                                              const float default_color[3]) {
    device_ = &device;

    // --- Load .obj via tinyobjloader ---
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanMeshLoadFailed,
                                "Failed to load .obj"};
    }
    if (!warn.empty()) {
        SNT_LOG_WARN(".obj warning: %s", warn.c_str());
    }

    // --- Build vertex + index arrays ---
    // Per-face colors for visual distinction (6 faces of the cube).
    // Face order in cube.obj: front(+Z), back(-Z), right(+X), left(-X),
    // top(+Y), bottom(-Y). Each face = 2 triangles = 6 vertices.
    static const float kFaceColors[6][3] = {
        {1.0f, 0.5f, 0.2f},  // front  (+Z) orange
        {0.2f, 0.8f, 1.0f},  // back   (-Z) cyan
        {0.9f, 0.2f, 0.3f},  // right  (+X) red
        {0.3f, 0.9f, 0.4f},  // left   (-X) green
        {0.95f, 0.9f, 0.3f}, // top    (+Y) yellow
        {0.6f, 0.3f, 0.9f},  // bottom (-Y) purple
    };

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    uint32_t vertex_index_in_face = 0;  // 0..5, resets each face

    // DIAGNOSTIC: log face index distribution.
    SNT_LOG_DEBUG("mesh load: %zu shapes, %zu total indices",
                  shapes.size(), shapes.empty() ? 0 : shapes[0].mesh.indices.size());

    for (const auto& shape : shapes) {
        for (size_t i = 0; i < shape.mesh.indices.size(); ++i) {
            const auto& index = shape.mesh.indices[i];
            MeshVertex vertex{};

            // Position (tinyobj stores positions as a flat float array).
            if (index.vertex_index >= 0) {
                vertex.position[0] = attrib.vertices[3 * index.vertex_index + 0];
                vertex.position[1] = attrib.vertices[3 * index.vertex_index + 1];
                vertex.position[2] = attrib.vertices[3 * index.vertex_index + 2];
            }

            // Color: pick by face. Each face has 6 vertices (2 triangles).
            // Fallback to default_color if face index out of range.
            uint32_t face_idx = static_cast<uint32_t>(i / 6);
            if (face_idx < 6) {
                vertex.color[0] = kFaceColors[face_idx][0];
                vertex.color[1] = kFaceColors[face_idx][1];
                vertex.color[2] = kFaceColors[face_idx][2];
            } else {
                vertex.color[0] = default_color[0];
                vertex.color[1] = default_color[1];
                vertex.color[2] = default_color[2];
            }

            // DIAGNOSTIC: log first 12 vertices' face assignment.
            if (i < 12) {
                SNT_LOG_DEBUG("  v[%zu] face=%u color=(%.2f,%.2f,%.2f) pos=(%.2f,%.2f,%.2f)",
                              i, face_idx,
                              vertex.color[0], vertex.color[1], vertex.color[2],
                              vertex.position[0], vertex.position[1], vertex.position[2]);
            }

            vertices.push_back(vertex);
            indices.push_back(static_cast<uint32_t>(indices.size()));
        }
    }

    vertex_count_ = static_cast<uint32_t>(vertices.size());
    index_count_ = static_cast<uint32_t>(indices.size());

    SNT_LOG_INFO("Mesh loaded: %s (%u verts, %u indices)",
                 path.c_str(), vertex_count_, index_count_);

    // DIAGNOSTIC: print first 4 vertices to verify position data.
    for (uint32_t i = 0; i < vertex_count_ && i < 4; ++i) {
        SNT_LOG_DEBUG("[diag mesh] v[%u] pos=(%.2f,%.2f,%.2f) color=(%.2f,%.2f,%.2f)",
                      i,
                      vertices[i].position[0], vertices[i].position[1],
                      vertices[i].position[2],
                      vertices[i].color[0], vertices[i].color[1],
                      vertices[i].color[2]);
    }

    // --- Create vertex buffer (CPU-visible for P1.5; P2: staging buffer) ---
    vertex_buffer_ = new VulkanBuffer();
    auto vb_result = vertex_buffer_->init(*device_, sizeof(MeshVertex) * vertices.size(),
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         true /* cpu_visible */);
    if (!vb_result) {
        return vb_result.error().with_context("VulkanMesh vertex buffer init");
    }
    vertex_buffer_->write(vertices.data(),
                          sizeof(MeshVertex) * vertices.size());

    // --- Create index buffer ---
    index_buffer_ = new VulkanBuffer();
    auto ib_result = index_buffer_->init(*device_, sizeof(uint32_t) * indices.size(),
                                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                         true /* cpu_visible */);
    if (!ib_result) {
        return ib_result.error().with_context("VulkanMesh index buffer init");
    }
    index_buffer_->write(indices.data(), sizeof(uint32_t) * indices.size());

    return {};
}

void VulkanMesh::destroy() {
    delete vertex_buffer_;  // VulkanBuffer destructor calls destroy()
    delete index_buffer_;
    vertex_buffer_ = nullptr;
    index_buffer_ = nullptr;
    vertex_count_ = 0;
    index_count_ = 0;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void VulkanMesh::draw(VkCommandBuffer cmd) {
    if (!vertex_buffer_ || !index_buffer_) return;

    // Bind vertex buffer.
    VkBuffer vertex_buffers[] = {vertex_buffer_->handle()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);

    // Bind index buffer.
    vkCmdBindIndexBuffer(cmd, index_buffer_->handle(), 0, VK_INDEX_TYPE_UINT32);

    // Draw indexed.
    vkCmdDrawIndexed(cmd, index_count_, 1, 0, 0, 0);
}

}  // namespace snt::render_backend
