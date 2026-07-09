// ChunkRenderer implementation.

#define SNT_LOG_CHANNEL "voxel"
#include "core/log.h"

#include "voxel/chunk_renderer.h"

#include "assets/material_atlas.h"
#include "assets/texture_cache.h"
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
//   location 4: uv          (vec2)  offset 32
// Total stride = 40 bytes (no padding; all 4-byte aligned).
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
        VkVertexInputAttributeDescription{
            .location = 4,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = offsetof(VoxelVertex, uv),
        },
    };
}

snt::core::Expected<void> copy_buffer_now(
        snt::render_backend::VulkanDevice& device,
        VkCommandPool command_pool,
        VkBuffer src,
        VkBuffer dst,
        VkDeviceSize size) {
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device.logical(), &alloc_info, &cmd) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                "ChunkRenderer: upload command buffer allocation failed"};
    }

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.logical(), command_pool, 1, &cmd);
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                "ChunkRenderer: upload command buffer begin failed"};
    }

    VkBufferCopy copy_region{.srcOffset = 0, .dstOffset = 0, .size = size};
    vkCmdCopyBuffer(cmd, src, dst, 1, &copy_region);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.logical(), command_pool, 1, &cmd);
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                "ChunkRenderer: upload command buffer end failed"};
    }

    VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device.logical(), &fence_info, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(device.logical(), command_pool, 1, &cmd);
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                "ChunkRenderer: upload fence creation failed"};
    }

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    if (vkQueueSubmit(device.graphics_queue(), 1, &submit_info, fence) != VK_SUCCESS) {
        vkDestroyFence(device.logical(), fence, nullptr);
        vkFreeCommandBuffers(device.logical(), command_pool, 1, &cmd);
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                "ChunkRenderer: upload queue submit failed"};
    }

    vkWaitForFences(device.logical(), 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device.logical(), fence, nullptr);
    vkFreeCommandBuffers(device.logical(), command_pool, 1, &cmd);
    return {};
}

snt::core::Expected<void> copy_buffer_to_image_now(
        snt::render_backend::VulkanDevice& device,
        VkCommandPool command_pool,
        VkBuffer src,
        VkImage dst,
        uint32_t width,
        uint32_t height) {
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device.logical(), &alloc_info, &cmd) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                "ChunkRenderer: image upload command buffer allocation failed"};
    }

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy copy{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(cmd, src, dst,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device.logical(), &fence_info, nullptr, &fence);
    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vkQueueSubmit(device.graphics_queue(), 1, &submit_info, fence);
    vkWaitForFences(device.logical(), 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device.logical(), fence, nullptr);
    vkFreeCommandBuffers(device.logical(), command_pool, 1, &cmd);
    return {};
}

snt::core::Expected<void> create_material_atlas_texture(
        snt::render_backend::VulkanDevice& device,
        VkCommandPool command_pool,
        VkImage& image,
        VmaAllocation& allocation,
        VkImageView& view,
        VkSampler& sampler) {
    snt::assets::TextureCache texture_cache;
    auto atlas_r = snt::assets::build_default_voxel_material_atlas(texture_cache);
    if (!atlas_r) {
        return atlas_r.error().with_context("ChunkRenderer::create_material_atlas_texture");
    }
    const auto& atlas = *atlas_r;

    VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {atlas.width, atlas.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(device.vma_allocator(), &image_info, &alloc_info,
                       &image, &allocation, nullptr) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanBufferInitFailed,
                                "ChunkRenderer: atlas image allocation failed"};
    }

    snt::render_backend::VulkanBuffer staging;
    const VkDeviceSize bytes = atlas.rgba.size();
    if (auto r = staging.init(device, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              /*cpu_visible=*/true); !r) {
        return r.error().with_context("ChunkRenderer atlas staging");
    }
    staging.write(atlas.rgba.data(), bytes);
    if (auto r = copy_buffer_to_image_now(device, command_pool, staging.handle(),
                                          image, atlas.width, atlas.height); !r) {
        return r.error().with_context("ChunkRenderer atlas upload");
    }

    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(device.logical(), &view_info, nullptr, &view) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanBufferInitFailed,
                                "ChunkRenderer: atlas image view failed"};
    }

    VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    };
    if (vkCreateSampler(device.logical(), &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanBufferInitFailed,
                                "ChunkRenderer: atlas sampler failed"};
    }

    SNT_LOG_INFO("Voxel material atlas uploaded (%ux%u)", atlas.width, atlas.height);
    return {};
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

    VkCommandPoolCreateInfo upload_pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device.graphics_family(),
    };
    if (vkCreateCommandPool(device.logical(), &upload_pool_info, nullptr,
                            &upload_command_pool_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandPoolFailed,
                                "ChunkRenderer::init: upload command pool failed"};
    }

    VmaAllocation atlas_alloc = VK_NULL_HANDLE;
    if (auto r = create_material_atlas_texture(device, upload_command_pool_,
                                               atlas_image_, atlas_alloc,
                                               atlas_view_, atlas_sampler_); !r) {
        return r;
    }
    atlas_allocation_ = atlas_alloc;

    if (auto r = descriptor_->init_with_texture(device, max_chunks,
                                                atlas_view_, atlas_sampler_); !r) {
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
    if (device_ && upload_command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_->logical(), upload_command_pool_, nullptr);
        upload_command_pool_ = VK_NULL_HANDLE;
    }
    if (pipeline_)    { pipeline_->destroy();   pipeline_.reset(); }
    if (descriptor_)  { descriptor_->destroy(); descriptor_.reset(); }
    if (device_ && atlas_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_->logical(), atlas_sampler_, nullptr);
        atlas_sampler_ = VK_NULL_HANDLE;
    }
    if (device_ && atlas_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_->logical(), atlas_view_, nullptr);
        atlas_view_ = VK_NULL_HANDLE;
    }
    if (device_ && atlas_image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(device_->vma_allocator(), atlas_image_,
                        static_cast<VmaAllocation>(atlas_allocation_));
        atlas_image_ = VK_NULL_HANDLE;
        atlas_allocation_ = nullptr;
    }
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
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               /*cpu_visible=*/false);
    if (!vbo_r) {
        free_mesh_slots_.push_back(slot_id);
        snt::core::Error e = vbo_r.error();
        e.with_context("ChunkRenderer::upload_mesh (vbo)");
        return e;
    }
    auto ibo_r = pool_->acquire(ibo_size,
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               /*cpu_visible=*/false);
    if (!ibo_r) {
        pool_->release(*vbo_r);
        free_mesh_slots_.push_back(slot_id);
        snt::core::Error e = ibo_r.error();
        e.with_context("ChunkRenderer::upload_mesh (ibo)");
        return e;
    }

    auto* vbo = pool_->get(*vbo_r);
    auto* ibo = pool_->get(*ibo_r);

    snt::render_backend::VulkanBuffer staging_vbo;
    if (auto r = staging_vbo.init(*device_, vbo_size,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  /*cpu_visible=*/true); !r) {
        pool_->release(*vbo_r);
        pool_->release(*ibo_r);
        free_mesh_slots_.push_back(slot_id);
        snt::core::Error e = r.error();
        e.with_context("ChunkRenderer::upload_mesh (staging vbo)");
        return e;
    }
    staging_vbo.write(mesh.vertices.data(), vbo_size);

    snt::render_backend::VulkanBuffer staging_ibo;
    if (auto r = staging_ibo.init(*device_, ibo_size,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  /*cpu_visible=*/true); !r) {
        pool_->release(*vbo_r);
        pool_->release(*ibo_r);
        free_mesh_slots_.push_back(slot_id);
        snt::core::Error e = r.error();
        e.with_context("ChunkRenderer::upload_mesh (staging ibo)");
        return e;
    }
    staging_ibo.write(mesh.indices.data(), ibo_size);

    if (auto r = copy_buffer_now(*device_, upload_command_pool_,
                                 staging_vbo.handle(), vbo->handle(), vbo_size); !r) {
        pool_->release(*vbo_r);
        pool_->release(*ibo_r);
        free_mesh_slots_.push_back(slot_id);
        snt::core::Error e = r.error();
        e.with_context("ChunkRenderer::upload_mesh (copy vbo)");
        return e;
    }
    if (auto r = copy_buffer_now(*device_, upload_command_pool_,
                                 staging_ibo.handle(), ibo->handle(), ibo_size); !r) {
        pool_->release(*vbo_r);
        pool_->release(*ibo_r);
        free_mesh_slots_.push_back(slot_id);
        snt::core::Error e = r.error();
        e.with_context("ChunkRenderer::upload_mesh (copy ibo)");
        return e;
    }

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
