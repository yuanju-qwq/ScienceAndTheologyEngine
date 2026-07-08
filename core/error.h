// Error type for the SNT engine's Result-based error handling.
//
// Design goals:
//   - Replace ad-hoc `bool + SNT_LOG_ERROR` patterns with a typed error
//     that carries an error code + human-readable message.
//   - Support context chaining: lower-level failures can be wrapped with
//     higher-level context without losing the original cause.
//   - Cheap to construct (no allocations in the common success path).
//   - Printable to stderr via SNT_LOG macros.
//
// Usage:
//   // Returning an error from a low-level function:
//   Expected<VulkanDevice*> VulkanDevice::init(...) {
//       if (vkCreateDevice(...) != VK_SUCCESS) {
//           return Error{ErrorCode::kVulkanInitFailed,
//                        "vkCreateDevice failed (res={})", res};
//       }
//       return &instance;  // implicit Ok
//   }
//
//   // Wrapping with context at a higher level:
//   Expected<void> Engine::init() {
//       auto dev = vk_device.init(...);
//       if (!dev) return dev.error().with_context("VulkanDevice init");
//       ...
//   }
//
//   // Inspecting an error:
//   if (!result) {
//       SNT_LOG_ERROR("init failed: %s", result.error().format().c_str());
//   }

#pragma once

#include "core/log.h"  // for SNT_LOG_* in log()

#include <format>
#include <string>
#include <string_view>

namespace snt::core {

// Engine-wide error codes. Add new codes here as new subsystems land.
// kUnknown is the catch-all for errors that don't fit a specific category.
enum class ErrorCode : int {
    kUnknown = 0,
    // Generic / cross-cutting.
    kInvalidArgument,
    kInvalidState,
    kNotImplemented,
    kCancelled,
    // Platform / windowing.
    kPlatformInitFailed,
    kWindowCreateFailed,
    kSurfaceCreateFailed,
    // Vulkan backend.
    kVulkanInitFailed,
    kVulkanDeviceInitFailed,
    kVulkanSwapchainInitFailed,
    kVulkanDepthInitFailed,
    kVulkanDescriptorInitFailed,
    kVulkanPipelineInitFailed,
    kVulkanFrameInitFailed,
    kVulkanBufferInitFailed,
    kVulkanMeshLoadFailed,
    kVulkanCommandPoolFailed,
    kVulkanCommandBufferFailed,
    kNoSuitableGpu,
    kNoGraphicsQueue,
    kNoSurfaceFormats,
    kExtensionNotSupported,
    kValidationLayerMissing,
    // Render layer.
    kRenderSystemInitFailed,
    // Asset layer.
    kAssetCacheInitFailed,
    kMeshLoadFailed,
    kRenderGraphInitFailed,
    kRenderGraphExecuteFailed,
    kRenderFrameFailed,
    kTransientPoolInitFailed,
    kPipelineCacheInitFailed,
    // Asset / I/O.
    kFileNotFound,
    kFileOpenFailed,
    kAssetLoadFailed,
    // Script (AngelScript integration).
    kScriptEngineInitFailed,
    kScriptCompileFailed,
    kScriptExecuteFailed,
    kScriptModuleNotFound,
};

// Error: an ErrorCode + message + optional chained context.
//
// Copyable + movable (std::string internally). Typically returned by value
// from functions returning Expected<T, Error>. The chained context is
// built up via with_context() as the error propagates up the call stack.
class Error {
public:
    Error() = default;
    Error(ErrorCode code, std::string_view message)
        : code_(code), message_(message) {}
    // Implicit construction from ErrorCode for terse `return ErrorCode::kFoo;`
    Error(ErrorCode code) : code_(code), message_(default_message(code)) {}

    ErrorCode code() const { return code_; }
    const std::string& message() const { return message_; }
    const std::string& context() const { return context_; }

    // Wrap this error with a higher-level context string. The original
    // message is preserved; `context_` accumulates a call-chain like
    // "Engine::init -> VulkanDevice::init -> vkCreateDevice".
    // Returns *this so callers can write `return err.with_context("...");`.
    Error& with_context(std::string_view ctx) {
        if (!context_.empty()) {
            context_ += " -> ";
        }
        context_ += ctx;
        return *this;
    }

    // Render the full error as a single string for logging.
    // Format: "[code] message (context)" or "[code] message" if no context.
    std::string format() const {
        std::string out = std::format("[{}] {}", code_name(code_), message_);
        if (!context_.empty()) {
            out += std::format(" (in {})", context_);
        }
        return out;
    }

    // Convenience: stream to SNT_LOG_ERROR via a channel.
    void log(const char* channel) const {
        SNT_LOG_ERROR(channel, "%s", format().c_str());
    }

private:
    ErrorCode   code_    = ErrorCode::kUnknown;
    std::string message_;
    std::string context_;

    // Stable string for each ErrorCode (used by format() + log output).
    static const char* code_name(ErrorCode c) {
        switch (c) {
            case ErrorCode::kUnknown:                     return "Unknown";
            case ErrorCode::kInvalidArgument:            return "InvalidArgument";
            case ErrorCode::kInvalidState:                return "InvalidState";
            case ErrorCode::kNotImplemented:              return "NotImplemented";
            case ErrorCode::kCancelled:                  return "Cancelled";
            case ErrorCode::kPlatformInitFailed:         return "PlatformInitFailed";
            case ErrorCode::kWindowCreateFailed:         return "WindowCreateFailed";
            case ErrorCode::kSurfaceCreateFailed:        return "SurfaceCreateFailed";
            case ErrorCode::kVulkanInitFailed:            return "VulkanInitFailed";
            case ErrorCode::kVulkanDeviceInitFailed:      return "VulkanDeviceInitFailed";
            case ErrorCode::kVulkanSwapchainInitFailed:  return "VulkanSwapchainInitFailed";
            case ErrorCode::kVulkanDepthInitFailed:       return "VulkanDepthInitFailed";
            case ErrorCode::kVulkanDescriptorInitFailed:  return "VulkanDescriptorInitFailed";
            case ErrorCode::kVulkanPipelineInitFailed:   return "VulkanPipelineInitFailed";
            case ErrorCode::kVulkanFrameInitFailed:       return "VulkanFrameInitFailed";
            case ErrorCode::kVulkanBufferInitFailed:      return "VulkanBufferInitFailed";
            case ErrorCode::kVulkanMeshLoadFailed:        return "VulkanMeshLoadFailed";
            case ErrorCode::kVulkanCommandPoolFailed:    return "VulkanCommandPoolFailed";
            case ErrorCode::kVulkanCommandBufferFailed:   return "VulkanCommandBufferFailed";
            case ErrorCode::kNoSuitableGpu:              return "NoSuitableGpu";
            case ErrorCode::kNoGraphicsQueue:            return "NoGraphicsQueue";
            case ErrorCode::kNoSurfaceFormats:           return "NoSurfaceFormats";
            case ErrorCode::kExtensionNotSupported:       return "ExtensionNotSupported";
            case ErrorCode::kValidationLayerMissing:     return "ValidationLayerMissing";
            case ErrorCode::kRenderSystemInitFailed:     return "RenderSystemInitFailed";
            case ErrorCode::kAssetCacheInitFailed:       return "AssetCacheInitFailed";
            case ErrorCode::kMeshLoadFailed:             return "MeshLoadFailed";
            case ErrorCode::kRenderGraphInitFailed:       return "RenderGraphInitFailed";
            case ErrorCode::kRenderGraphExecuteFailed:    return "RenderGraphExecuteFailed";
            case ErrorCode::kRenderFrameFailed:           return "RenderFrameFailed";
            case ErrorCode::kTransientPoolInitFailed:    return "TransientPoolInitFailed";
            case ErrorCode::kPipelineCacheInitFailed:     return "PipelineCacheInitFailed";
            case ErrorCode::kFileNotFound:                return "FileNotFound";
            case ErrorCode::kFileOpenFailed:              return "FileOpenFailed";
            case ErrorCode::kAssetLoadFailed:             return "AssetLoadFailed";
            case ErrorCode::kScriptEngineInitFailed:      return "ScriptEngineInitFailed";
            case ErrorCode::kScriptCompileFailed:        return "ScriptCompileFailed";
            case ErrorCode::kScriptExecuteFailed:        return "ScriptExecuteFailed";
            case ErrorCode::kScriptModuleNotFound:        return "ScriptModuleNotFound";
        }
        return "Unknown";
    }

    // Default message when only an ErrorCode is given.
    static std::string default_message(ErrorCode c) {
        return code_name(c);
    }
};

}  // namespace snt::core
