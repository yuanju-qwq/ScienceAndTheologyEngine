// ChunkRenderer — GPU resource manager + draw recorder for voxel chunks.
//
// Owns the voxel graphics pipeline, a dedicated descriptor (one dynamic
// UBO slot per chunk for MVP), and a VertexBufferPool that recycles
// chunk VBO/IBO buffers across remeshes. The ECS ChunkRenderSystem
// drives this: remesh dirty chunks (upload_mesh), then each frame build
// a ChunkDrawCall list and call render().
//
// Layering: sits in voxel/, depends on render_backend (pipeline +
// descriptor + buffer pool) + voxel_mesh (VoxelMeshData). No ECS
// knowledge — the system layer feeds it draw calls.

#pragma once

#include "core/expected.h"        // Expected<T, Error>
#include "voxel/voxel_vertex.h"   // VoxelMeshData

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snt::render_backend {
class VulkanDevice;
class VulkanPipeline;
class VulkanDescriptor;
class VertexBufferPool;
}

namespace snt::voxel {

// Opaque handle to a chunk's uploaded GPU mesh. 0xFFFFFFFF = invalid/empty.
// Returned by upload_mesh; pass to render() in ChunkDrawCall and to
// unload_mesh when the chunk unloads.
struct ChunkMeshHandle {
    static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;
    uint32_t id = kInvalidId;

    bool valid() const { return id != kInvalidId; }
};

// One chunk draw for render(). `model` is the chunk's world transform
// (row-major 4x4, matches UniformBufferObject.model layout). The view
// and proj matrices are per-frame, shared across all chunks.
struct ChunkDrawCall {
    ChunkMeshHandle mesh_handle;
    float model[16];
};

class ChunkRenderer {
public:
    ChunkRenderer() = default;
    // Destructor defined in the .cpp so the forward-declared VulkanPipeline
    // / VulkanDescriptor complete types are visible when unique_ptr drops.
    ~ChunkRenderer();

    ChunkRenderer(const ChunkRenderer&) = delete;
    ChunkRenderer& operator=(const ChunkRenderer&) = delete;

    // Create the voxel pipeline + chunk descriptor + buffer pool.
    // `max_chunks` caps the per-frame UBO slot count (one MVP per chunk).
    // `vert_spv_path` / `frag_spv_path` are the voxel shader SPIR-V files.
    snt::core::Expected<void> init(snt::render_backend::VulkanDevice& device,
                                   VkFormat color_format,
                                   VkFormat depth_format,
                                   const std::string& vert_spv_path,
                                   const std::string& frag_spv_path,
                                   uint32_t max_chunks);

    // Release pipeline + descriptor + pool. Idempotent. Must be called
    // before the VulkanDevice passed to init() is destroyed.
    void destroy();

    // Upload a chunk mesh to the GPU. Allocates a VBO + IBO from the pool
    // and writes the vertex/index data. Returns a handle for later draw /
    // unload. An empty mesh (0 indices) returns an invalid handle without
    // allocating — the caller should skip drawing it.
    snt::core::Expected<ChunkMeshHandle> upload_mesh(const VoxelMeshData& mesh);

    // Release a chunk's GPU buffers back to the pool. Safe to call with an
    // invalid handle (no-op). The handle becomes invalid after unload.
    void unload_mesh(ChunkMeshHandle handle);

    // Render one frame's worth of chunks. Writes each chunk's MVP into the
    // dynamic UBO for `frame_idx`, then binds the voxel pipeline + records
    // a vkCmdDrawIndexed per chunk. Must be called inside a render pass
    // scope (the command buffer is in recording state). `draw_count` must
    // be <= max_chunks.
    void render(VkCommandBuffer cmd, uint32_t frame_idx,
                const float view[16], const float proj[16],
                const ChunkDrawCall* draws, uint32_t draw_count);

    uint32_t max_chunks() const { return max_chunks_; }

private:
    snt::render_backend::VulkanDevice* device_ = nullptr;
    VkCommandPool upload_command_pool_ = VK_NULL_HANDLE;

    // Owned GPU resources. Forward-declared types to keep glm/Vulkan out
    // of this header; full definitions live in the .cpp.
    std::unique_ptr<snt::render_backend::VulkanPipeline>   pipeline_;
    std::unique_ptr<snt::render_backend::VulkanDescriptor> descriptor_;
    std::unique_ptr<snt::render_backend::VertexBufferPool> pool_;

    uint32_t max_chunks_ = 0;

    // Per-chunk uploaded mesh: VBO/IBO pool-slot ids + index count.
    // Indexed by ChunkMeshHandle.id. A slot with valid=false is free.
    // The raw uint32 ids are wrapped back into VertexBufferHandle in the
    // .cpp when calling pool methods (keeps VertexBufferPool's full
    // definition out of this header).
    struct ChunkMesh {
        uint32_t vbo_id       = 0xFFFFFFFFu;
        uint32_t ibo_id       = 0xFFFFFFFFu;
        uint32_t index_count  = 0;
        bool     valid        = false;
    };
    std::vector<ChunkMesh> meshes_;
    std::vector<uint32_t>  free_mesh_slots_;
};

}  // namespace snt::voxel
