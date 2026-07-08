// Vulkan Graphics Pipeline — mesh vertex + fragment shader + depth state.
//
// P1.5: 3D mesh rendering with MVP UBO + depth testing.
// P1.4 was depth-less; P1.5 adds depth + descriptor set layout.
//
// P2.3 (option B): switched from VkRenderPass-based pipeline creation to
// dynamic rendering (Vulkan 1.3 core). The pipeline is created with a
// VkPipelineRenderingCreateInfo chained via pNext, specifying color +
// depth formats. `renderPass` field is VK_NULL_HANDLE. This removes the
// dependency on VulkanRenderPass and allows the pipeline to be used with
// any render pass scope established via vkCmdBeginRendering.

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace snt::render_backend {

class VulkanDevice;
class VulkanDescriptor;
class VulkanBuffer;

// Mesh vertex layout: 3D position + 3-component color (interleaved).
struct MeshVertex;

class VulkanPipeline {
public:
    VulkanPipeline() = default;
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    // Create the pipeline + pipeline layout with descriptor set layout.
    // `color_format` / `depth_format` feed VkPipelineRenderingCreateInfo;
    // pass VK_FORMAT_UNDEFINED for depth_format to disable depth testing.
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> init(VulkanDevice& device,
                                   VulkanDescriptor& descriptor,
                                   VkFormat color_format,
                                   VkFormat depth_format,
                                   const std::string& vert_spv_path,
                                   const std::string& frag_spv_path);

    void destroy();

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return pipeline_layout_; }

private:
    VkShaderModule create_shader_module(const std::string& path);

    VulkanDevice* device_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
};

}  // namespace snt::render_backend
