// Vulkan Descriptor — descriptor set layout + pool + sets for UBO binding.
//
// P2.4: upgraded to dynamic UBO to support multiple mesh entities.
//   - One large UBO buffer per frame-in-flight, holding N MVP matrices
//     back-to-back (N = kMaxEntities).
//   - Descriptor type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC.
//   - At draw time, RenderSystem passes a dynamic offset (entity_index *
//     aligned_stride) to vkCmdBindDescriptorSets.
//
// Layout:
//   binding 0: uniform buffer (dynamic) — MVP matrix
//
// P3+ will add: storage buffers (entity data), combined image samplers.

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace snt::render_backend {

class VulkanDevice;
class VulkanBuffer;

// UBO data structure — must match the vertex shader's uniform layout.
struct UniformBufferObject {
    float model[16];      // 4x4 model matrix
    float view[16];       // 4x4 view matrix
    float proj[16];       // 4x4 projection matrix
};

class VulkanDescriptor {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    VulkanDescriptor() = default;
    ~VulkanDescriptor();

    VulkanDescriptor(const VulkanDescriptor&) = delete;
    VulkanDescriptor& operator=(const VulkanDescriptor&) = delete;

    // Create descriptor set layout + pool + sets + dynamic UBO buffers.
    // `max_entities` controls the dynamic UBO slot count (one MVP per
    // entity); sourced from EngineConfig::RenderConfig::max_entities.
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> init(VulkanDevice& device, uint32_t max_entities);
    snt::core::Expected<void> init_with_texture(VulkanDevice& device,
                                                uint32_t max_entities,
                                                VkImageView image_view,
                                                VkSampler sampler);

    void destroy();

    // Runtime limit for entities supported per frame (set at init).
    uint32_t max_entities() const { return max_entities_; }

    // Update the MVP for entity `entity_index` in frame `frame_index`.
    // The dynamic UBO holds kMaxEntities MVP slots; this writes one.
    void update_ubo(uint32_t frame_index, uint32_t entity_index,
                    const UniformBufferObject& ubo);

    // Aligned stride between consecutive MVP slots in the UBO. Required
    // by VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC (minUboAlignment).
    uint32_t ubo_stride() const { return ubo_stride_; }

    VkDescriptorSetLayout layout() const { return descriptor_set_layout_; }
    VkDescriptorSet descriptor_set(uint32_t frame_index) const {
        return descriptor_sets_[frame_index];
    }

private:
    snt::core::Expected<void> init_internal(VulkanDevice& device,
                                            uint32_t max_entities,
                                            VkImageView image_view,
                                            VkSampler sampler,
                                            bool with_texture);

    VulkanDevice* device_ = nullptr;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets_;  // size = kMaxFramesInFlight

    // Dynamic UBO buffers (one per frame in flight), VMA-backed.
    // Each buffer holds max_entities_ * ubo_stride_ bytes.
    std::vector<VulkanBuffer*> ubo_buffers_;

    // Max mesh entities supported per frame (set at init from config).
    uint32_t max_entities_ = 256;

    // Stride between MVP slots. >= sizeof(UniformBufferObject), aligned
    // to device's minUniformBufferOffsetAlignment.
    uint32_t ubo_stride_ = sizeof(UniformBufferObject);
};

}  // namespace snt::render_backend
