// MuiRenderer implementation — font atlas texture + Vulkan pipeline + draw.

#include "mui_renderer.h"


#include "vulkan_buffer.h"
#include "vulkan_device.h"

#include "core/log.h"
#include "core/path_utils.h"

#include <volk.h>

#include <cstring>
#include <fstream>
#include <functional>
#include <vector>

namespace snt::ui {

// Helper: create a one-shot command buffer, submit it, and wait for
// completion. Used for the font atlas staging upload (init-time only).
static bool one_time_submit(snt::render_backend::VulkanDevice& device,
                            std::function<void(VkCommandBuffer)> recorder) {
    VkCommandPoolCreateInfo pool_ci{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device.graphics_family(),
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device.logical(), &pool_ci, nullptr, &pool) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device.logical(), &alloc_info, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(device.logical(), pool, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    bool ok = vkBeginCommandBuffer(cmd, &begin_info) == VK_SUCCESS;
    if (ok) recorder(cmd);
    if (ok) ok = vkEndCommandBuffer(cmd) == VK_SUCCESS;
    if (ok) {
        VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
        };
        ok = vkQueueSubmit(device.graphics_queue(), 1, &submit_info, VK_NULL_HANDLE) == VK_SUCCESS;
    }
    if (ok) ok = vkQueueWaitIdle(device.graphics_queue()) == VK_SUCCESS;

    vkDestroyCommandPool(device.logical(), pool, nullptr);
    return ok;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

MuiRenderer::~MuiRenderer() {
    destroy();
}

snt::core::Expected<void> MuiRenderer::init(
    snt::render_backend::VulkanDevice& device,
    VkFormat color_format) {

    device_ = &device;

    // 1. Create the Unicode glyph atlas Vulkan texture.
    if (auto r = create_glyph_atlas_texture(); !r) {
        return r.error();
    }

    // 2. Create descriptor set (UBO + sampler).
    if (auto r = create_descriptors(); !r) {
        return r.error();
    }

    // 3. Create graphics pipeline.
    if (auto r = create_pipeline(color_format); !r) {
        return r.error();
    }

    SNT_LOG_INFO("MuiRenderer initialized (Unicode atlas=%ux%u RGBA)",
                 UiGlyphAtlas::kDimension, UiGlyphAtlas::kDimension);
    return {};
}

void MuiRenderer::destroy() {
    if (!device_) return;
    VkDevice dev = device_->logical();

    device_->wait_idle();

    if (pipeline_)        { vkDestroyPipeline(dev, pipeline_, nullptr);             pipeline_ = VK_NULL_HANDLE; }
    if (pipeline_layout_) { vkDestroyPipelineLayout(dev, pipeline_layout_, nullptr); pipeline_layout_ = VK_NULL_HANDLE; }
    if (desc_pool_)       { vkDestroyDescriptorPool(dev, desc_pool_, nullptr);       desc_pool_ = VK_NULL_HANDLE; }
    if (desc_layout_)     { vkDestroyDescriptorSetLayout(dev, desc_layout_, nullptr); desc_layout_ = VK_NULL_HANDLE; }

    if (ubo_buffer_)  { vmaDestroyBuffer(device_->vma_allocator(), ubo_buffer_,  ubo_alloc_);  ubo_buffer_ = VK_NULL_HANDLE; }
    if (vbo_)         { vmaDestroyBuffer(device_->vma_allocator(), vbo_,         vbo_alloc_);  vbo_ = VK_NULL_HANDLE; }
    if (ibo_)         { vmaDestroyBuffer(device_->vma_allocator(), ibo_,         ibo_alloc_);  ibo_ = VK_NULL_HANDLE; }

    if (atlas_sampler_) { vkDestroySampler(dev, atlas_sampler_, nullptr); atlas_sampler_ = VK_NULL_HANDLE; }
    if (atlas_view_)    { vkDestroyImageView(dev, atlas_view_, nullptr);  atlas_view_ = VK_NULL_HANDLE; }
    if (atlas_image_)   { vmaDestroyImage(device_->vma_allocator(), atlas_image_, atlas_allocation_); atlas_image_ = VK_NULL_HANDLE; }

    uploaded_atlas_revision_ = 0;
    uploaded_atlas_ = nullptr;
    device_ = nullptr;
}

// ---------------------------------------------------------------------------
// Font atlas baking
// ---------------------------------------------------------------------------

snt::core::Expected<void> MuiRenderer::create_glyph_atlas_texture() {
    constexpr uint32_t kDimension = UiGlyphAtlas::kDimension;
    VkImageCreateInfo image_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {kDimension, kDimension, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(device_->vma_allocator(), &image_ci, &alloc_ci,
                       &atlas_image_, &atlas_allocation_, nullptr) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vmaCreateImage (Unicode glyph atlas) failed"};
    }

    VkImageViewCreateInfo view_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = atlas_image_,
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
    if (vkCreateImageView(device_->logical(), &view_ci, nullptr, &atlas_view_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vkCreateImageView (Unicode glyph atlas) failed"};
    }

    if (!one_time_submit(*device_, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = atlas_image_,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    })) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "Initial Unicode glyph atlas transition failed"};
    }

    VkSamplerCreateInfo sampler_ci{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    };
    if (vkCreateSampler(device_->logical(), &sampler_ci, nullptr, &atlas_sampler_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vkCreateSampler (Unicode glyph atlas) failed"};
    }
    return {};
}

snt::core::Expected<void> MuiRenderer::upload_glyph_atlas(const UiGlyphAtlas& atlas) {
    if (atlas.width != UiGlyphAtlas::kDimension || atlas.height != UiGlyphAtlas::kDimension ||
        atlas.rgba.size() != static_cast<size_t>(atlas.width) * atlas.height * 4u) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Invalid Unicode glyph atlas upload payload"};
    }

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_alloc = VK_NULL_HANDLE;
    VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = atlas.rgba.size(),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo allocation_ci{};
    allocation_ci.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    if (vmaCreateBuffer(device_->vma_allocator(), &buffer_ci, &allocation_ci,
                        &staging, &staging_alloc, nullptr) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vmaCreateBuffer (Unicode glyph staging) failed"};
    }
    void* mapped = nullptr;
    if (vmaMapMemory(device_->vma_allocator(), staging_alloc, &mapped) != VK_SUCCESS) {
        vmaDestroyBuffer(device_->vma_allocator(), staging, staging_alloc);
        return snt::core::Error{snt::core::ErrorCode::kVulkanBufferInitFailed,
                                "vmaMapMemory (Unicode glyph staging) failed"};
    }
    std::memcpy(mapped, atlas.rgba.data(), atlas.rgba.size());
    vmaUnmapMemory(device_->vma_allocator(), staging_alloc);

    const bool submitted = one_time_submit(*device_, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = atlas_image_,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy region{
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
            .imageExtent = {atlas.width, atlas.height, 1},
        };
        vkCmdCopyBufferToImage(cmd, staging, atlas_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    });
    vmaDestroyBuffer(device_->vma_allocator(), staging, staging_alloc);
    if (!submitted) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "Unicode glyph atlas upload submission failed"};
    }
    return {};
}// ---------------------------------------------------------------------------
// Descriptor set (UBO + combined image sampler)
// ---------------------------------------------------------------------------

snt::core::Expected<void> MuiRenderer::create_descriptors() {
    VkDevice dev = device_->logical();

    // Layout: binding 0 = UBO (ortho), binding 1 = combined image sampler.
    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo layout_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings,
    };
    if (vkCreateDescriptorSetLayout(dev, &layout_ci, nullptr, &desc_layout_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vkCreateDescriptorSetLayout (ui) failed"};
    }

    // Pool.
    VkDescriptorPoolSize pool_sizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
    };
    VkDescriptorPoolCreateInfo pool_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes,
    };
    if (vkCreateDescriptorPool(dev, &pool_ci, nullptr, &desc_pool_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vkCreateDescriptorPool (ui) failed"};
    }

    // Allocate one descriptor set.
    VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = desc_pool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &desc_layout_,
    };
    if (vkAllocateDescriptorSets(dev, &alloc_info, &desc_set_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vkAllocateDescriptorSets (ui) failed"};
    }

    // Create UBO buffer (4x4 float matrix = 64 bytes, host-visible).
    VkBufferCreateInfo ubo_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 64,  // 4x4 float matrix
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo ubo_alloc_ci{};
    ubo_alloc_ci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    if (vmaCreateBuffer(device_->vma_allocator(), &ubo_ci, &ubo_alloc_ci,
                        &ubo_buffer_, &ubo_alloc_, nullptr) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vmaCreateBuffer (ui ubo) failed"};
    }

    // Update descriptor set: bind UBO + sampler.
    VkDescriptorBufferInfo ubo_info{
        .buffer = ubo_buffer_,
        .offset = 0,
        .range = 64,
    };
    VkDescriptorImageInfo img_info{
        .sampler = atlas_sampler_,
        .imageView = atlas_view_,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = desc_set_,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &ubo_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = desc_set_,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &img_info,
        },
    };
    vkUpdateDescriptorSets(dev, 2, writes, 0, nullptr);

    return {};
}

// ---------------------------------------------------------------------------
// Graphics pipeline (alpha blend, no depth)
// ---------------------------------------------------------------------------

snt::core::Expected<void> MuiRenderer::create_pipeline(VkFormat color_format) {
    VkDevice dev = device_->logical();

    // Pipeline layout: one descriptor set (UBO + sampler).
    VkPipelineLayoutCreateInfo layout_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &desc_layout_,
    };
    if (vkCreatePipelineLayout(dev, &layout_ci, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vkCreatePipelineLayout (ui) failed"};
    }

    // Shader stages.
    auto read_spv = [](const std::string& path) -> std::vector<uint32_t> {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return {};
        size_t size = static_cast<size_t>(f.tellg());
        std::vector<uint32_t> spv(size / 4);
        f.seekg(0);
        f.read(reinterpret_cast<char*>(spv.data()), size);
        return spv;
    };

    auto vert_spv = read_spv(snt::core::path_utils::resolve("shaders/ui.vert.spv"));
    auto frag_spv = read_spv(snt::core::path_utils::resolve("shaders/ui.frag.spv"));
    if (vert_spv.empty() || frag_spv.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "Failed to load UI shaders (ui.vert.spv / ui.frag.spv)"};
    }

    VkShaderModuleCreateInfo vert_ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vert_spv.size() * 4,
        .pCode = vert_spv.data(),
    };
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    vkCreateShaderModule(dev, &vert_ci, nullptr, &vert_mod);

    VkShaderModuleCreateInfo frag_ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = frag_spv.size() * 4,
        .pCode = frag_spv.data(),
    };
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    vkCreateShaderModule(dev, &frag_ci, nullptr, &frag_mod);

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_mod,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_mod,
            .pName = "main",
        },
    };

    // Vertex input: pos2D + atlas UV + RGBA + explicit texture mode.
    VkVertexInputBindingDescription binding{
        .binding = 0,
        .stride = sizeof(UiVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attrs[4] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(UiVertex, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(UiVertex, uv),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(UiVertex, color),
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R8_UINT,
            .offset = offsetof(UiVertex, texture_mode),
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 4,
        .pVertexAttributeDescriptions = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    // No viewport/scissor in pipeline state — using dynamic state.
    VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo raster{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    // Alpha blending: src * srcAlpha + dst * (1 - srcAlpha).
    VkPipelineColorBlendAttachmentState blend_attach{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attach,
    };

    VkDynamicState dyn_states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dyn_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states,
    };

    // Dynamic rendering info (no render pass).
    VkPipelineRenderingCreateInfo rendering_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
        // No depth attachment for UI.
    };

    VkGraphicsPipelineCreateInfo pipeline_ci{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_ci,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dyn_state,
        .layout = pipeline_layout_,
    };

    VkResult err = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipeline_ci,
                                             nullptr, &pipeline_);

    vkDestroyShaderModule(dev, vert_mod, nullptr);
    vkDestroyShaderModule(dev, frag_mod, nullptr);

    if (err != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kUnknown,
                                "vkCreateGraphicsPipelines (ui) failed"};
    }
    return {};
}

// ---------------------------------------------------------------------------
// Per-frame update + render
// ---------------------------------------------------------------------------

void MuiRenderer::update_ortho(uint32_t fb_width, uint32_t fb_height) {
    // Orthographic projection: pixel space (top-left origin, Y down) -> Vulkan clip space.
    // With a positive-height Vulkan viewport, NDC y=-1 lands at the top of
    // the framebuffer and y=+1 lands at the bottom. Keep pixel Y increasing
    // downward so UI layout and font quads stay upright.
    float w = static_cast<float>(fb_width);
    float h = static_cast<float>(fb_height);
    float ortho[16] = {
        2.0f / w, 0.0f,      0.0f, 0.0f,
        0.0f,     2.0f / h,  0.0f, 0.0f,
        0.0f,     0.0f,     -1.0f, 0.0f,
       -1.0f,    -1.0f,      0.0f, 1.0f,
    };

    void* mapped = nullptr;
    vmaMapMemory(device_->vma_allocator(), ubo_alloc_, &mapped);
    std::memcpy(mapped, ortho, sizeof(ortho));
    vmaUnmapMemory(device_->vma_allocator(), ubo_alloc_);
}

snt::core::Expected<void> MuiRenderer::synchronize_glyph_atlas(const UiDrawData& draw_data) {
    if (!draw_data.glyph_atlas) return {};
    const UiGlyphAtlas& atlas = *draw_data.glyph_atlas;
    if (uploaded_atlas_ == &atlas && uploaded_atlas_revision_ == atlas.revision) return {};

    if (auto result = upload_glyph_atlas(atlas); !result) {
        return result.error().with_context("MuiRenderer::synchronize_glyph_atlas");
    }
    uploaded_atlas_ = &atlas;
    uploaded_atlas_revision_ = atlas.revision;
    SNT_LOG_INFO("MUI Unicode glyph atlas synchronized (revision=%llu)",
                 static_cast<unsigned long long>(atlas.revision));
    return {};
}

void MuiRenderer::render(VkCommandBuffer cmd, const UiDrawData& draw_data) {
    if (draw_data.vertices.empty() || draw_data.indices.empty()) return;

    // Ensure VBO is large enough (recreate if needed).
    VkDeviceSize vbo_needed = draw_data.vertices.size() * sizeof(UiVertex);
    if (vbo_needed > vbo_size_) {
        if (vbo_) {
            vmaDestroyBuffer(device_->vma_allocator(), vbo_, vbo_alloc_);
        }
        VkBufferCreateInfo ci{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = vbo_needed,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        vmaCreateBuffer(device_->vma_allocator(), &ci, &aci,
                        &vbo_, &vbo_alloc_, nullptr);
        vbo_size_ = vbo_needed;
    }

    // Ensure IBO is large enough.
    VkDeviceSize ibo_needed = draw_data.indices.size() * sizeof(uint16_t);
    if (ibo_needed > ibo_size_) {
        if (ibo_) {
            vmaDestroyBuffer(device_->vma_allocator(), ibo_, ibo_alloc_);
        }
        VkBufferCreateInfo ci{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = ibo_needed,
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        vmaCreateBuffer(device_->vma_allocator(), &ci, &aci,
                        &ibo_, &ibo_alloc_, nullptr);
        ibo_size_ = ibo_needed;
    }

    // Upload vertex + index data.
    void* vbo_mapped = nullptr;
    vmaMapMemory(device_->vma_allocator(), vbo_alloc_, &vbo_mapped);
    std::memcpy(vbo_mapped, draw_data.vertices.data(), vbo_needed);
    vmaUnmapMemory(device_->vma_allocator(), vbo_alloc_);

    void* ibo_mapped = nullptr;
    vmaMapMemory(device_->vma_allocator(), ibo_alloc_, &ibo_mapped);
    std::memcpy(ibo_mapped, draw_data.indices.data(), ibo_needed);
    vmaUnmapMemory(device_->vma_allocator(), ibo_alloc_);

    // Bind pipeline + descriptor set.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &desc_set_, 0, nullptr);

    // Bind vertex + index buffers.
    VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbo_, offsets);
    vkCmdBindIndexBuffer(cmd, ibo_, 0, VK_INDEX_TYPE_UINT16);

    // Draw.
    vkCmdDrawIndexed(cmd,
                     static_cast<uint32_t>(draw_data.indices.size()),
                     1, 0, 0, 0);
}

}  // namespace snt::ui
