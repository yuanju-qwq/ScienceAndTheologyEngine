// Vulkan Instance — creates VkInstance, debug messenger, and loads
// Vulkan function pointers via Volk.
//
// P1.3 responsibilities:
//   - volkInitialize() to load vulkan-1.dll
//   - Create VkInstance with SDL's required surface extensions
//   - Enable VK_LAYER_KHRONOS_validation in Debug builds
//   - Register debug messenger to forward validation messages to stderr
//
// Usage:
//   VulkanInstance inst;
//   if (!inst.init(window)) { /* fail */ }
//   // inst.handle() is now valid; volk has all instance-level functions.

#pragma once

#include "core/expected.h"  // Expected<T, Error>

#include <vulkan/vulkan.h>  // types + VK_NO_PROTOTYPES (via volk target)

#include <cstdint>

namespace snt::platform { class Window; }

namespace snt::render_backend {

class VulkanInstance {
public:
    VulkanInstance() = default;
    ~VulkanInstance();

    // Non-copyable; RAII.
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    // Initialize Volk, create VkInstance + debug messenger.
    // `window` provides the SDL-required instance extensions.
    // Returns void on success, or an Error describing the failure.
    snt::core::Expected<void> init(snt::platform::Window& window);

    // Destroy instance + messenger. Called automatically by destructor.
    void destroy();

    VkInstance handle() const { return instance_; }
    bool is_valid() const { return instance_ != VK_NULL_HANDLE; }

private:
    // Debug messenger callback: forwards validation layer messages to stderr.
    static VKAPI_ATTR VkBool32 VKAPI_CALL
    debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                   VkDebugUtilsMessageTypeFlagsEXT type,
                   const VkDebugUtilsMessengerCallbackDataEXT* data,
                   void* user_data);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
};

}  // namespace snt::render_backend
