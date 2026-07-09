// Vertex Buffer Pool implementation.

#define SNT_LOG_CHANNEL "render_backend"
#include "core/log.h"

#include "vertex_buffer_pool.h"
#include "vulkan_buffer.h"
#include "vulkan_device.h"

#include <algorithm>

namespace snt::render_backend {

VertexBufferPool::~VertexBufferPool() {
    destroy();
}

snt::core::Expected<void> VertexBufferPool::init(VulkanDevice* device) {
    if (!device) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "VertexBufferPool::init: null device"};
    }
    device_ = device;
    return {};
}

void VertexBufferPool::destroy() {
    slots_.clear();  // unique_ptr<VulkanBuffer> releases each buffer
    device_ = nullptr;
}

snt::core::Expected<VertexBufferHandle> VertexBufferPool::acquire(
        VkDeviceSize min_size, VkBufferUsageFlags usage, bool cpu_visible) {
    if (!device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "VertexBufferPool::acquire: not initialized"};
    }

    // Reuse scan: pick the smallest free slot whose flags match and whose
    // size is >= min_size. Smallest-first reduces internal fragmentation
    // (we don't hand a 1MB buffer to a 4KB chunk).
    uint32_t best_slot = VertexBufferHandle::kInvalidId;
    VkDeviceSize best_size = static_cast<VkDeviceSize>(-1);
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        const Slot& s = slots_[i];
        if (s.in_use) continue;
        if (s.usage != usage || s.cpu_visible != cpu_visible) continue;
        if (s.size < min_size) continue;
        if (s.size < best_size) {
            best_size = s.size;
            best_slot = i;
        }
    }

    if (best_slot != VertexBufferHandle::kInvalidId) {
        slots_[best_slot].in_use = true;
        return VertexBufferHandle{best_slot};
    }

    // No reusable slot: allocate a new VulkanBuffer.
    Slot slot;
    slot.buffer = std::make_unique<VulkanBuffer>();
    auto r = slot.buffer->init(*device_, min_size, usage, cpu_visible);
    if (!r) {
        snt::core::Error e = r.error();
        e.with_context("VertexBufferPool::acquire");
        return e;
    }
    slot.size        = min_size;
    slot.usage       = usage;
    slot.cpu_visible = cpu_visible;
    slot.in_use      = true;

    const uint32_t id = static_cast<uint32_t>(slots_.size());
    slots_.push_back(std::move(slot));
    return VertexBufferHandle{id};
}

void VertexBufferPool::release(VertexBufferHandle handle) {
    if (!handle.valid()) return;
    if (handle.id >= slots_.size()) return;
    slots_[handle.id].in_use = false;
}

VulkanBuffer* VertexBufferPool::get(VertexBufferHandle handle) const {
    if (!handle.valid()) return nullptr;
    if (handle.id >= slots_.size()) return nullptr;
    const Slot& s = slots_[handle.id];
    return s.in_use ? s.buffer.get() : nullptr;
}

uint32_t VertexBufferPool::in_use_count() const {
    uint32_t n = 0;
    for (const auto& s : slots_) if (s.in_use) ++n;
    return n;
}

}  // namespace snt::render_backend
