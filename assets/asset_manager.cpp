// AssetManager implementation.

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"
#include "core/path_utils.h"

#include "assets/asset_manager.h"

#include "render_backend/vulkan_device.h"

namespace snt::assets {

AssetManager& AssetManager::instance() {
    static AssetManager inst;
    return inst;
}

snt::core::Expected<void> AssetManager::init_mesh_cache() {
    mesh_loader_.init(device_);
    // Wire the mesh cache with closures over the loader.
    if (auto r = mesh_cache_.init(
            [this](const std::string& path) { return mesh_loader_.load(path); },
            [this](snt::render_backend::VulkanMesh* m) { mesh_loader_.destroy(m); });
        !r) {
        snt::core::Error e = r.error();
        e.with_context("AssetManager::init_mesh_cache");
        return e;
    }
    return {};
}

snt::core::Expected<void> AssetManager::init(snt::render_backend::VulkanDevice* device) {
    if (!device) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "AssetManager::init: null device"};
    }
    if (device_) {
        // Already initialized; re-init is a no-op (idempotent).
        return {};
    }
    device_ = device;
    if (auto r = init_mesh_cache(); !r) {
        return r;
    }
    SNT_LOG_INFO("AssetManager initialized (device=%p)",
                 static_cast<const void*>(device));
    return {};
}

snt::core::Expected<void> AssetManager::init_from_manifest(
    snt::render_backend::VulkanDevice* device,
    const std::string& manifest_path) {
    if (!device) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "AssetManager::init_from_manifest: null device"};
    }
    if (device_) {
        // Already initialized; re-init is a no-op (idempotent).
        return {};
    }
    device_ = device;
    if (auto r = init_mesh_cache(); !r) {
        return r;
    }

    // Load the manifest. Missing file is non-fatal (falls back to runtime
    // load()); only parse errors or duplicate ids abort.
    const std::string resolved = snt::core::path_utils::resolve(manifest_path);
    auto manifest_result = load_manifest(resolved);
    if (!manifest_result) {
        snt::core::Error e = manifest_result.error();
        e.with_context("AssetManager::init_from_manifest");
        return e;
    }
    const AssetManifest& manifest = *manifest_result;

    // Pre-allocate handles in manifest order so handle N always refers
    // to the asset declared at position N. Scene files that reference
    // assets by handle are then stable across runs.
    for (const auto& entry : manifest.entries) {
        const std::string resolved_path =
            snt::core::path_utils::resolve(entry.path);
        mesh_cache_.register_preallocated(resolved_path);
    }

    // Eagerly load all pre-allocated assets to the GPU so the first
    // frame doesn't stall on disk IO.
    if (auto r = mesh_cache_.load_preallocated(); !r) {
        snt::core::Error e = r.error();
        e.with_context("AssetManager::init_from_manifest (load_preallocated)");
        return e;
    }

    SNT_LOG_INFO("AssetManager initialized from manifest '%s' (device=%p, %zu assets)",
                 manifest_path.c_str(),
                 static_cast<const void*>(device),
                 manifest.entries.size());
    return {};
}

void AssetManager::shutdown() {
    if (!device_) return;
    // Wait for the GPU to finish using any cached resources before
    // their backing buffers are torn down. This matches the original
    // MeshCache::destroy() prelude.
    device_->wait_idle();
    mesh_cache_.destroy();
    device_ = nullptr;
    SNT_LOG_INFO("AssetManager shut down");
}

}  // namespace snt::assets
