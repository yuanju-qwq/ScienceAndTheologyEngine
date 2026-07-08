// PipelineCache: caches VkPipeline objects by string key.
//
// Avoids recreating pipelines every frame when the same shader/state is
// reused. P2.1: skeleton only — init() / lookup() / insert() are declared
// but empty. P2.4 will wire this into ForwardPass to cache the mesh
// pipeline.
//
// Reference: KhronosGroup/Vulkan-Samples framework's PipelineCache uses a
// similar string-keyed map for shader-pipeline reuse.

#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <unordered_map>

namespace snt::render_backend {
class VulkanDevice;
}

namespace snt::renderer {

class PipelineCache {
public:
    PipelineCache() = default;
    ~PipelineCache();

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    // Initialize with a device reference. Pipelines are created against
    // this device.
    void init(snt::render_backend::VulkanDevice& device);

    // Destroy all cached pipelines. Does NOT destroy the device.
    void destroy();

    // Lookup a cached pipeline by key. Returns VK_NULL_HANDLE if absent.
    VkPipeline lookup(const std::string& key) const;

    // Insert a pipeline under key. Takes ownership of the VkPipeline.
    // If key already exists, the old pipeline is destroyed first.
    void insert(const std::string& key, VkPipeline pipeline);

private:
    snt::render_backend::VulkanDevice* device_ = nullptr;
    std::unordered_map<std::string, VkPipeline> cache_;
};

}  // namespace snt::renderer
