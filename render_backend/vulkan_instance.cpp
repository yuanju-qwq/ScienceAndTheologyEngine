// Vulkan Instance implementation.

#define SNT_LOG_CHANNEL "render_backend"
#include "core/log.h"

#include "vulkan_instance.h"

#include "platform/window.h"

#include <volk.h>  // must come after <vulkan/vulkan.h> types are available

#include <cstring>
#include <vector>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Debug messenger callback
// ---------------------------------------------------------------------------

VKAPI_ATTR VkBool32 VKAPI_CALL
VulkanInstance::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                               VkDebugUtilsMessageTypeFlagsEXT type,
                               const VkDebugUtilsMessengerCallbackDataEXT* data,
                               void* /*user_data*/) {
    // Forward validation messages to the engine logger. Verbose/info
    // severities are skipped to reduce noise; warnings+errors are emitted
    // via SNT_LOG_WARN. ERROR-severity messages could be promoted to
    // SNT_LOG_ERROR if desired, but Vulkan validation tends to emit
    // both as "best-effort" diagnostics, so WARN keeps them grouped.
    (void)type;  // message category unused here
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        SNT_LOG_WARN("Vulkan validation: %s", data->pMessage);
    }
    return VK_FALSE;  // don't abort the call
}

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanInstance::~VulkanInstance() {
    destroy();
}

snt::core::Expected<void> VulkanInstance::init(snt::platform::Window& window) {
    // --- Step 1: Volk initialize (loads vulkan-1.dll via LoadLibrary) ---
    if (volkInitialize() != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanInitFailed,
                                "volkInitialize failed: vulkan-1.dll not found"};
    }

    // --- Step 2: gather instance extensions ---
    // SDL provides the platform-specific surface extensions (e.g.
    // VK_KHR_win32_surface, VK_KHR_surface).
    uint32_t sdl_ext_count = 0;
    const char* const* sdl_exts = window.sdl_vulkan_instance_extensions(&sdl_ext_count);

    std::vector<const char*> extensions;
    extensions.reserve(sdl_ext_count + 1);
    for (uint32_t i = 0; i < sdl_ext_count; ++i) {
        extensions.push_back(sdl_exts[i]);
    }

    // Debug utils extension for the messenger.
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    // --- Step 3: validation layers (Debug only) ---
    std::vector<const char*> layers;
#ifdef NDEBUG
    // Release: no validation layer.
#else
    // Check if the layer is available before requesting it.
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    bool has_validation = false;
    for (const auto& l : available_layers) {
        if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            has_validation = true;
            break;
        }
    }
    if (has_validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        SNT_LOG_INFO("Validation layer: enabled");
    } else {
        SNT_LOG_WARN("VK_LAYER_KHRONOS_validation not available");
    }
#endif

    // --- Step 4: create VkInstance ---
    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ScienceAndTheology",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "SNT Engine",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanInitFailed,
                                "vkCreateInstance failed"};
    }

    // --- Step 5: load instance-level function pointers via Volk ---
    volkLoadInstance(instance_);

    // --- Step 6: register debug messenger ---
    if (!layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT messenger_info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debug_callback,
            .pUserData = nullptr,
        };
        if (vkCreateDebugUtilsMessengerEXT(instance_, &messenger_info, nullptr,
                                           &debug_messenger_) != VK_SUCCESS) {
            SNT_LOG_WARN("failed to create debug messenger");
            // Non-fatal: continue without messenger.
        }
    }

    SNT_LOG_INFO("VkInstance created (Vulkan 1.3)");
    return {};
}

void VulkanInstance::destroy() {
    if (instance_ == VK_NULL_HANDLE) return;

    if (debug_messenger_ != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
        debug_messenger_ = VK_NULL_HANDLE;
    }
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
}

}  // namespace snt::render_backend
