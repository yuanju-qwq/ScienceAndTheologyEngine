// Vertex Buffer Pool — reusable VMA-backed buffers for streaming chunk meshes.
//
// Purpose: avoid allocate/destroy churn when chunks mesh and unmesh. Each
// chunk holds a VBO + IBO; when a chunk remeshes or unloads, its buffers
// return to the pool. A subsequent chunk of similar size reuses them
// instead of hitting vmaCreateBuffer / vmaDestroyBuffer again.
//
// Design:
//   - Pool owns all VulkanBuffer instances. Callers hold BufferHandle
//     (a slot index); the pool never invalidates a live handle.
//   - acquire(min_size, usage, cpu_visible) returns a buffer whose actual
//     size is >= min_size. Reuse policy: a free slot is eligible when its
//     usage + cpu_visible flags match and its size >= min_size. The
//     smallest eligible slot is picked to reduce waste. If none is free,
//     a new VulkanBuffer is allocated.
//   - release(handle) marks the slot free; the underlying VulkanBuffer
//     stays alive until the pool is destroyed or the slot is recycled.
//   - destroy() frees every slot (called by owner at shutdown).
//
// Layering: sits beside vulkan_buffer in render_backend. Depends only on
// VulkanDevice + VulkanBuffer + core. No ECS, no voxel types.

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace snt::render_backend {

class VulkanDevice;
class VulkanBuffer;

// Opaque handle into the pool. 0xFFFFFFFF = invalid.
struct VertexBufferHandle {
    static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;
    uint32_t id = kInvalidId;

    bool valid() const { return id != kInvalidId; }
};

class VertexBufferPool {
public:
    VertexBufferPool() = default;
    ~VertexBufferPool();

    VertexBufferPool(const VertexBufferPool&) = delete;
    VertexBufferPool& operator=(const VertexBufferPool&) = delete;

    // Bind to a Vulkan device. The device is borrowed (not owned) and
    // must outlive the pool. Idempotent.
    snt::core::Expected<void> init(VulkanDevice* device);

    // Release every pooled buffer. Idempotent. Must be called before the
    // VulkanDevice passed to init() is destroyed.
    void destroy();

    // Acquire a buffer of at least `min_size` bytes with `usage` flags.
    // `cpu_visible` = true selects host-visible memory (for direct CPU
    // writes); false selects device-local memory (GPU-only, needs staging).
    // Returns a handle on success or an Error on allocation failure.
    snt::core::Expected<VertexBufferHandle> acquire(
        VkDeviceSize min_size, VkBufferUsageFlags usage, bool cpu_visible);

    // Return a buffer to the pool. It becomes eligible for reuse by a
    // future acquire() with matching usage + cpu_visible + size. The
    // handle becomes invalid after release.
    void release(VertexBufferHandle handle);

    // Look up the underlying VulkanBuffer for a handle. Returns nullptr
    // for invalid or released handles.
    VulkanBuffer* get(VertexBufferHandle handle) const;

    // Number of slots currently in use (for diagnostics / debug panel).
    uint32_t in_use_count() const;

private:
    struct Slot {
        std::unique_ptr<VulkanBuffer> buffer;
        VkDeviceSize         size         = 0;
        VkBufferUsageFlags   usage        = 0;
        bool                 cpu_visible  = false;
        bool                 in_use       = false;
    };

    VulkanDevice* device_ = nullptr;
    std::vector<Slot> slots_;
};

}  // namespace snt::render_backend
