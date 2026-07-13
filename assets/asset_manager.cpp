// AssetManager implementation.

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/asset_manager.h"

#include "render_backend/vulkan_device.h"

#include <utility>

namespace snt::assets {

snt::core::Expected<void> AssetManager::init_mesh_cache() {
    if (!device_ || !source_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "AssetManager::init_mesh_cache: missing device or source"};
    }
    mesh_loader_.init(device_);
    // Cache keys remain source-relative requests until GPU residency owns its
    // canonical identity map. The loader itself always receives owned source
    // bytes and never resolves an ambient game path.
    if (auto r = mesh_cache_.init(
            [this](const std::string& request_path)
                -> snt::core::Expected<snt::render_backend::VulkanMesh*> {
                auto source_data = source_->read(
                    AssetSourceRequest{.requested_path = request_path});
                if (!source_data) {
                    snt::core::Error error = source_data.error();
                    error.with_context(
                        "AssetManager::mesh_source('" + request_path + "')");
                    return error;
                }
                return mesh_loader_.load(std::move(*source_data));
            },
            [this](snt::render_backend::VulkanMesh* m) { mesh_loader_.destroy(m); });
        !r) {
        snt::core::Error e = r.error();
        e.with_context("AssetManager::init_mesh_cache");
        return e;
    }
    return {};
}

snt::core::Expected<void> AssetManager::init(
    snt::render_backend::VulkanDevice* device,
    IAssetSource& source,
    const AssetCatalog& catalog) {
    if (!device) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "AssetManager::init: null device"};
    }
    if (device_) {
        // Already initialized; re-init is a no-op (idempotent).
        return {};
    }

    device_ = device;
    source_ = &source;
    if (auto r = init_mesh_cache(); !r) {
        snt::core::Error error = r.error();
        device_ = nullptr;
        source_ = nullptr;
        return error;
    }

    // Pre-allocate source-relative requests in catalog order. Scene loading
    // uses the same relative request strings, so a declared asset resolves to
    // its stable cache slot before the first frame.
    for (const auto& entry : catalog.manifest().entries) {
        mesh_cache_.register_preallocated(entry.path);
    }

    // Eagerly load declared assets so the first frame does not perform file
    // I/O or GPU allocation. Each load is routed through IAssetSource.
    if (auto r = mesh_cache_.load_preallocated(); !r) {
        snt::core::Error error = r.error();
        error.with_context("AssetManager::init(load_preallocated)");
        mesh_cache_.destroy();
        device_ = nullptr;
        source_ = nullptr;
        return error;
    }

    SNT_LOG_INFO("AssetManager initialized from catalog '%s' (device=%p, %zu declared, %zu cached)",
                 catalog.manifest_identity().c_str(),
                 static_cast<const void*>(device),
                 catalog.size(),
                 mesh_cache_.size());
    return {};
}

void AssetManager::shutdown() {
    if (!device_) {
        source_ = nullptr;
        return;
    }
    // Wait for the GPU to finish using any cached resources before
    // their backing buffers are torn down. This matches the original
    // MeshCache::destroy() prelude.
    device_->wait_idle();
    mesh_cache_.destroy();
    device_ = nullptr;
    source_ = nullptr;
    SNT_LOG_INFO("AssetManager shut down");
}

}  // namespace snt::assets
