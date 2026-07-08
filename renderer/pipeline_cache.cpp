// PipelineCache implementation — P2.1 skeleton.
// init() / destroy() / lookup() / insert() are stubs. P2.4 wires real
// VkPipeline creation via VulkanPipeline + caches results here.

#define SNT_LOG_CHANNEL "renderer"
#include "core/log.h"

#include "renderer/pipeline_cache.h"

#include "render_backend/vulkan_device.h"

namespace snt::renderer {

PipelineCache::~PipelineCache() {
    destroy();
}

void PipelineCache::init(snt::render_backend::VulkanDevice& device) {
    device_ = &device;
    SNT_LOG_INFO("PipelineCache initialized (P2.1 skeleton)");
}

void PipelineCache::destroy() {
    // P2.1 stub: cache_ is empty so nothing to free.
    // P2.4 will call vkDestroyPipeline for each entry via device_.
    cache_.clear();
    device_ = nullptr;
}

VkPipeline PipelineCache::lookup(const std::string& key) const {
    auto it = cache_.find(key);
    if (it == cache_.end()) return VK_NULL_HANDLE;
    return it->second;
}

void PipelineCache::insert(const std::string& key, VkPipeline pipeline) {
    // P2.1 stub: store the pipeline. Real resource cleanup lands in P2.4.
    cache_[key] = pipeline;
}

}  // namespace snt::renderer
