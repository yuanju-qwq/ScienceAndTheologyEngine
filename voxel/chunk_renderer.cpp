// ChunkRenderer implementation.

#define SNT_LOG_CHANNEL "voxel"
#include "core/log.h"

#include "voxel/chunk_renderer.h"

#include "render_backend/vulkan_buffer.h"
#include "render_backend/vulkan_descriptor.h"
#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_pipeline.h"
#include "render_backend/vertex_buffer_pool.h"

#include <volk.h>

#include <cstring>
#include <new>

namespace snt::voxel {

namespace {

// VoxelVertex vertex input layout for the voxel pipeline.
// Matches voxel.vert attribute locations:
//   location 0: position    (vec3)  offset 0
//   location 1: normal      (vec3)  offset 12
//   location 2: material_id (uint)  offset 24
//   location 3: face_type   (float) offset 28
// Total stride = 32 bytes (no padding; all 4-byte aligned).
VkVertexInputBindingDescription voxel_binding() {
    return VkVertexInputBindingDescription{
        .binding    = 0,
        .stride     = sizeof(VoxelVertex),
        .inputRate  = VK_VERTEX_INPUT_RATE_VERTEX,
    };
}

std::vector<VkVertexInputAttributeDescription> voxel_attributes() {
    return {
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = offsetof(VoxelVertex, position),
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = offsetof(VoxelVertex, normal),
        },
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding  = 0,
            .format   = VK_FORMAT_R32_UINT,
            .offset   = offsetof(VoxelVertex, material_id),
        },
        VkVertexInputAttributeDescription{
            .location = 3,
            .binding  = 0,
            .format   = VK_FORMAT_R32_SFLOAT,
            .offset   = offsetof(VoxelVertex, face_type),
        },
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ChunkRenderer::~ChunkRenderer() {
    destroy();
}

snt::core::Expected<void> ChunkRenderer::init(
        snt::render_backend::VulkanDevice& device,
        VkFormat color_format,
        VkFormat depth_format,
        const std::string& vert_spv_path,
        const std::string& frag_spv_path,
        uint32_t max_chunks) {
    device_ = &device;
    max_chunks_ = max_chunks;

    // --- Descriptor: one dynamic UBO slot per chunk ---
    pipeline_  = std::make_unique<snt::render_backend::VulkanPipeline>();
    descriptor_ = std::make_unique<snt::render_backend::VulkanDescriptor>();
    pool_      = std::make_unique<snt::render_backend::VertexBufferPool>();

    if (auto r = descriptor_->init(device, max_chunks); !r) {
        snt::core::Error e = r.error();
        e.with_context("ChunkRenderer::init (descriptor)");
        return e;
    }

    // --- Pipeline: voxel shaders + VoxelVertex layout ---
    if (auto r = pipeline_->init(device, *descriptor_,
                                 color_format, depth_format,
                                 vert_spv_path, frag_spv_path,
                                 voxel_binding(), voxel_attributes()); !r) {
        snt::core::Error e = r.error();
        e.with_context("ChunkRenderer::init (pipeline)");
        return e;
    }

    // --- Buffer pool ---
    if (auto r = pool_->init(&device); !r) {
        snt::core::Error e = r.error();
        e.with_context("ChunkRenderer::init (pool)");
        return e;
    }

    SNT_LOG_INFO("ChunkRenderer initialized (max_chunks=%u)", max_chunks);
    return {};
}

void ChunkRenderer::destroy() {
    // Release any still-loaded chunk meshes back to the pool.
    for (auto& m : meshes_) {
        if (m.valid) {
            pool_->release(snt::render_backend::VertexBufferHandle{m.vbo_id});
            pool_->release(snt::render_backend::VertexBufferHandle{m.ibo_id});
            m.valid = false;
        }
    }
    meshes_.clear();
    free_mesh_slots_.clear();

    if (pool_)        { pool_->destroy();       pool_.reset(); }
    if (pipeline_)    { pipeline_->destroy();   pipeline_.reset(); }
    if (descriptor_)  { descriptor_->destroy(); descriptor_.reset(); }
    device_ = nullptr;
    max_chunks_ = 0;
}

// ---------------------------------------------------------------------------
// Mesh upload / unload
// ---------------------------------------------------------------------------

snt::core::Expected<ChunkMeshHandle> ChunkRenderer::upload_mesh(
        const VoxelMeshData& mesh) {
    if (!device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ChunkRenderer::upload_mesh: not initialized"};
    }
    // Empty mesh: nothing to draw. Return invalid handle so the caller
    // skips this chunk in render().
    if (mesh.indices.empty() || mesh.vertices.empty()) {
        return ChunkMeshHandle{};
    }

    // Allocate a mesh slot (reuse a freed one if available).
    uint32_t slot_id;
    if (!free_mesh_slots_.empty()) {
        slot_id = free_mesh_slots_.back();
        free_mesh_slots_.pop_back();
    } else {
        slot_id = static_cast<uint32_t>(meshes_.size());
        meshes_.push_back({});
    }

    const VkDeviceSize vbo_size = sizeof(VoxelVertex) * mesh.vertices.size();
    const VkDeviceSize ibo_size = sizeof(uint32_t)    * mesh.indices.size();

    auto vbo_r = pool_->acquire(vbo_size,
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               /*cpu_visible=*/true);
    if (!vbo_r) {
        free_mesh_slots_.push_back(slot_id);
        snt::core::Error e = vbo_r.error();
        e.with_context("ChunkRenderer::upload_mesh (vbo)");
        return e;
    }
    auto ibo_r = pool_->acquire(ibo_size,
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               /*cpu_visible=*/true);
    if (!ibo_r) {
        pool_->release(*vbo_r);
        free_mesh_slots_.push_back(slot_id);
        snt::core::Error e = ibo_r.error();
        e.with_context("ChunkRenderer::upload_mesh (ibo)");
        return e;
    }

    auto* vbo = pool_->get(*vbo_r);
    auto* ibo = pool_->get(*ibo_r);
    vbo->write(mesh.vertices.data(), vbo_size);
    ibo->write(mesh.indices.data(),  ibo_size);

    auto& slot = meshes_[slot_id];
    slot.vbo_id       = vbo_r->id;
    slot.ibo_id       = ibo_r->id;
    slot.index_count  = static_cast<uint32_t>(mesh.indices.size());
    slot.valid        = true;

    return ChunkMeshHandle{slot_id};
}

void ChunkRenderer::unload_mesh(ChunkMeshHandle handle) {
    if (!handle.valid()) return;
    if (handle.id >= meshes_.size()) return;
    auto& m = meshes_[handle.id];
    if (!m.valid) return;
    pool_->release(snt::render_backend::VertexBufferHandle{m.vbo_id});
    pool_->release(snt::render_backend::VertexBufferHandle{m.ibo_id});
    m.valid = false;
    free_mesh_slots_.push_back(handle.id);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void ChunkRenderer::render(VkCommandBuffer cmd, uint32_t frame_idx,
                           const float view[16], const float proj[16],
                           const ChunkDrawCall* draws, uint32_t draw_count) {
    if (!pipeline_ || !descriptor_) return;
    if (draw_count == 0) return;
    if (draw_count > max_chunks_) {
        SNT_LOG_ERROR("ChunkRenderer::render: draw_count=%u > max_chunks=%u",
                      draw_count, max_chunks_);
        draw_count = max_chunks_;
    }

    VkPipeline pipe = pipeline_->handle();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

    VkDescriptorSet desc_set = descriptor_->descriptor_set(frame_idx);
    const uint32_t ubo_stride = descriptor_->ubo_stride();

    // Write each chunk's MVP into its UBO slot, then bind + draw.
    // UBO writes are host-coherent memcpys; they complete before the GPU
    // reads them at draw-execute time (after submit), so doing them during
    // command buffer recording is safe.
    for (uint32_t i = 0; i < draw_count; ++i) {
        const auto& d = draws[i];
        if (!d.mesh_handle.valid() || d.mesh_handle.id >= meshes_.size()) {
            continue;
        }
        const auto& mesh = meshes_[d.mesh_handle.id];
        if (!mesh.valid || mesh.index_count == 0) {
            continue;
        }

        // Build + write the UBO for this chunk's slot.
        snt::render_backend::UniformBufferObject ubo{};
        std::memcpy(ubo.model, d.model, sizeof(ubo.model));
        std::memcpy(ubo.view,  view,    sizeof(ubo.view));
        std::memcpy(ubo.proj,  proj,    sizeof(ubo.proj));
        descriptor_->update_ubo(frame_idx, i, ubo);

        // Bind this chunk's VBO + IBO.
        auto* vbo = pool_->get(snt::render_backend::VertexBufferHandle{mesh.vbo_id});
        auto* ibo = pool_->get(snt::render_backend::VertexBufferHandle{mesh.ibo_id});
        if (!vbo || !ibo) continue;

        VkBuffer vbo_handles[] = {vbo->handle()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vbo_handles, offsets);
        vkCmdBindIndexBuffer(cmd, ibo->handle(), 0, VK_INDEX_TYPE_UINT32);

        // Bind descriptor set with dynamic offset = slot * ubo_stride.
        uint32_t dyn_offset = i * ubo_stride;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_->layout(), 0, 1, &desc_set,
                                1, &dyn_offset);

        vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
    }
}

}  // namespace snt::voxel
