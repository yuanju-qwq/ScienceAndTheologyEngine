// Vulkan Graphics Pipeline implementation.

#define SNT_LOG_CHANNEL "render_backend"
#include "core/log.h"

#include "vulkan_pipeline.h"
#include "vulkan_descriptor.h"
#include "vulkan_device.h"

#include <volk.h>

#include <fstream>
#include <vector>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Helper: load SPIR-V file
// ---------------------------------------------------------------------------

static std::vector<char> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        SNT_LOG_ERROR("Failed to open shader: %s", path.c_str());
        return {};
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

// ---------------------------------------------------------------------------
// Helper: create shader module from SPIR-V bytes
// ---------------------------------------------------------------------------

VkShaderModule VulkanPipeline::create_shader_module(const std::string& path) {
    auto bytes = read_file(path);
    if (bytes.empty()) return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes.size(),
        .pCode = reinterpret_cast<const uint32_t*>(bytes.data()),
    };

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_->logical(), &create_info, nullptr, &module)
        != VK_SUCCESS) {
        SNT_LOG_ERROR("vkCreateShaderModule failed: %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    return module;
}

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanPipeline::~VulkanPipeline() {
    destroy();
}

snt::core::Expected<void> VulkanPipeline::init(VulkanDevice& device,
                                               VulkanDescriptor& descriptor,
                                               VkFormat color_format,
                                               VkFormat depth_format,
                                               const std::string& vert_spv_path,
                                               const std::string& frag_spv_path,
                                               const VkVertexInputBindingDescription& binding,
                                               const std::vector<VkVertexInputAttributeDescription>& attributes) {
    device_ = &device;

    // --- Step 1: load shader modules ---
    VkShaderModule vert_module = create_shader_module(vert_spv_path);
    VkShaderModule frag_module = create_shader_module(frag_spv_path);
    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        if (vert_module) vkDestroyShaderModule(device_->logical(), vert_module, nullptr);
        if (frag_module) vkDestroyShaderModule(device_->logical(), frag_module, nullptr);
        return snt::core::Error{snt::core::ErrorCode::kVulkanPipelineInitFailed,
                                "Failed to load shader modules"};
    }

    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module,
            .pName = "main",
        },
    };

    // --- Step 2: vertex input (caller-supplied layout) ---
    VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };

    // --- Step 3: input assembly (triangle list) ---
    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // --- Step 4: viewport + scissor (dynamic) ---
    VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    // --- Step 5: rasterizer ---
    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    // --- Step 6: multisampling (disabled) ---
    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    // --- Step 7: depth + stencil (NEW in P1.5) ---
    VkPipelineDepthStencilStateCreateInfo depth_stencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    // --- Step 8: color blend ---
    VkPipelineColorBlendAttachmentState blend_attachment{
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    // --- Step 9: dynamic state ---
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    // --- Step 10: pipeline layout (with descriptor set layout) ---
    VkDescriptorSetLayout set_layouts[] = {descriptor.layout()};
    VkPipelineLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 0,
    };

    if (vkCreatePipelineLayout(device_->logical(), &layout_info, nullptr,
                               &pipeline_layout_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanPipelineInitFailed,
                                "vkCreatePipelineLayout failed"};
    }

    // --- Step 11: create graphics pipeline (dynamic rendering, P2.3) ---
    // VkPipelineRenderingCreateInfo specifies attachment formats without
    // a VkRenderPass object. `renderPass` is VK_NULL_HANDLE.
    VkPipelineRenderingCreateInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
        .depthAttachmentFormat = depth_format,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
    };

    VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout_,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
    };

    if (vkCreateGraphicsPipelines(device_->logical(), VK_NULL_HANDLE, 1,
                                  &pipeline_info, nullptr, &pipeline_)
        != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanPipelineInitFailed,
                                "vkCreateGraphicsPipelines failed"};
    }

    vkDestroyShaderModule(device_->logical(), vert_module, nullptr);
    vkDestroyShaderModule(device_->logical(), frag_module, nullptr);

    SNT_LOG_INFO("Graphics pipeline created (dynamic rendering)");
    return {};
}

void VulkanPipeline::destroy() {
    if (!device_ || device_->logical() == VK_NULL_HANDLE) return;
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_->logical(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_->logical(), pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
}

}  // namespace snt::render_backend
