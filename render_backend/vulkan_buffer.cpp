// Vulkan Buffer implementation.

#define SNT_LOG_CHANNEL "render_backend"
#include "core/log.h"

#include "vulkan_buffer.h"
#include "vulkan_device.h"

#include <volk.h>

#include <cstring>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanBuffer::~VulkanBuffer() {
    destroy();
}

snt::core::Expected<void> VulkanBuffer::init(VulkanDevice& device, VkDeviceSize size,
                                              VkBufferUsageFlags usage, bool cpu_visible) {
    device_ = &device;
    size_ = size;

    VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_info{};
    if (cpu_visible) {
        alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    } else {
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    }

    // VMA allocator is stored on the device; access via a getter (added below).
    if (vmaCreateBuffer(device_->vma_allocator(), &buffer_info, &alloc_info,
                        &buffer_, &allocation_, nullptr) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanBufferInitFailed,
                                "vmaCreateBuffer failed"};
    }

    return {};
}

void VulkanBuffer::destroy() {
    if (buffer_ != VK_NULL_HANDLE && device_) {
        vmaDestroyBuffer(device_->vma_allocator(), buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
    size_ = 0;
}

// ---------------------------------------------------------------------------
// Map / unmap / write
// ---------------------------------------------------------------------------

void* VulkanBuffer::map() {
    if (buffer_ == VK_NULL_HANDLE) return nullptr;
    void* mapped = nullptr;
    if (vmaMapMemory(device_->vma_allocator(), allocation_, &mapped) != VK_SUCCESS) {
        SNT_LOG_ERROR("vmaMapMemory failed");
        return nullptr;
    }
    return mapped;
}

void VulkanBuffer::unmap() {
    if (allocation_ != VK_NULL_HANDLE) {
        vmaUnmapMemory(device_->vma_allocator(), allocation_);
    }
}

void VulkanBuffer::write(const void* data, VkDeviceSize data_size) {
    if (data_size > size_) {
        SNT_LOG_ERROR("Buffer write overflow: %llu > %llu",
                      static_cast<unsigned long long>(data_size),
                      static_cast<unsigned long long>(size_));
        return;
    }
    void* mapped = map();
    if (mapped) {
        std::memcpy(mapped, data, static_cast<size_t>(data_size));
        unmap();
    }
}

void VulkanBuffer::write_at(const void* data, VkDeviceSize data_size,
                            VkDeviceSize offset) {
    if (offset + data_size > size_) {
        SNT_LOG_ERROR("Buffer write_at overflow: offset=%llu size=%llu > buf=%llu",
                      static_cast<unsigned long long>(offset),
                      static_cast<unsigned long long>(data_size),
                      static_cast<unsigned long long>(size_));
        return;
    }
    void* mapped = map();
    if (mapped) {
        std::memcpy(static_cast<uint8_t*>(mapped) + offset, data,
                    static_cast<size_t>(data_size));
        unmap();
    }
}

}  // namespace snt::render_backend
