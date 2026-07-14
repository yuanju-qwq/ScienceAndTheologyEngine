// AssetManager implementation.

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/asset_manager.h"

#include "render_backend/vulkan_device.h"

#include <utility>

namespace snt::assets {

snt::core::Expected<void> AssetManager::init(
    snt::render_backend::VulkanDevice* device,
    IAssetSource& source,
    const AssetCatalog& catalog) {
    if (!device) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "AssetManager::init: null device"};
    }
    if (device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "AssetManager::init: already initialized"};
    }
    if (auto result = uploader_.init(device); !result) {
        snt::core::Error error = result.error();
        error.with_context("AssetManager::init(uploader)");
        return error;
    }

    device_ = device;
    source_ = &source;
    for (const auto& entry : catalog.manifest().entries) {
        if (auto handle = resolve_mesh(entry.path); !handle) {
            snt::core::Error error = handle.error();
            error.with_context("AssetManager::init(preload '" + entry.path + "')");
            shutdown();
            return error;
        }
    }

    SNT_LOG_INFO("AssetManager initialized from catalog '%s' (device=%p, %zu declared, %zu mesh handles, %zu GPU meshes)",
                 catalog.manifest_identity().c_str(),
                 static_cast<const void*>(device),
                 catalog.size(),
                 mesh_count(),
                 uploader_.resident_count());
    return {};
}

snt::core::Expected<MeshHandle> AssetManager::resolve_mesh(
    std::string requested_path) {
    if (!device_ || !source_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "AssetManager::resolve_mesh: not initialized"};
    }
    if (requested_path.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "AssetManager::resolve_mesh: empty path"};
    }

    if (const auto existing = mesh_references_.find_mesh(requested_path)) {
        if (existing->id >= mesh_tokens_.size() ||
            !mesh_tokens_[existing->id].valid()) {
            SNT_LOG_ERROR("Mesh reference '%s' has no active GPU residency token",
                          requested_path.c_str());
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "AssetManager::resolve_mesh: mesh reference has no active residency"};
        }
        return *existing;
    }

    auto source_data = source_->read(AssetSourceRequest{.requested_path = requested_path});
    if (!source_data) {
        snt::core::Error error = source_data.error();
        error.with_context("AssetManager::resolve_mesh(source '" + requested_path + "')");
        return error;
    }

    auto token = uploader_.upload(GpuAssetUploadRequest{
        .kind = GpuAssetKind::kMesh,
        .source = std::move(*source_data),
    });
    if (!token) {
        snt::core::Error error = token.error();
        error.with_context("AssetManager::resolve_mesh(upload '" + requested_path + "')");
        return error;
    }

    auto handle = mesh_references_.resolve_mesh(requested_path);
    if (!handle) {
        snt::core::Error error = handle.error();
        if (auto release = uploader_.release(*token); !release) {
            SNT_LOG_WARN("Failed to release mesh upload token after reference registration failure: %s",
                         release.error().format().c_str());
        }
        error.with_context("AssetManager::resolve_mesh(reference '" + requested_path + "')");
        return error;
    }
    if (handle->id != mesh_tokens_.size()) {
        SNT_LOG_ERROR("Mesh reference handle allocation lost ordering for '%s' (id=%u, token slots=%zu)",
                      requested_path.c_str(), handle->id, mesh_tokens_.size());
        if (auto release = uploader_.release(*token); !release) {
            SNT_LOG_WARN("Failed to release mesh upload token after ordering mismatch: %s",
                         release.error().format().c_str());
        }
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "AssetManager::resolve_mesh: mesh handle ordering mismatch"};
    }

    mesh_tokens_.push_back(*token);
    return *handle;
}

std::string AssetManager::mesh_path(MeshHandle handle) const {
    return mesh_references_.mesh_path(handle);
}

snt::render_backend::VulkanMesh* AssetManager::mesh(
    MeshHandle handle) const noexcept {
    if (!handle.valid() || handle.id >= mesh_tokens_.size()) {
        return nullptr;
    }
    return uploader_.mesh(mesh_tokens_[handle.id]);
}

std::size_t AssetManager::mesh_count() const noexcept {
    return mesh_references_.size();
}

void AssetManager::shutdown() {
    const bool was_initialized = device_ != nullptr;
    if (device_) {
        // release()/evict_unused() may destroy backing buffers, so no submitted
        // command buffer may still reference an active residency entry.
        device_->wait_idle();
    }

    for (const auto token : mesh_tokens_) {
        if (!token.valid()) {
            continue;
        }
        if (auto release = uploader_.release(token); !release) {
            SNT_LOG_WARN("Failed to release GPU mesh token %llu during AssetManager shutdown: %s",
                         static_cast<unsigned long long>(token.value),
                         release.error().format().c_str());
        }
    }
    if (device_) {
        if (auto evicted = uploader_.evict_unused(); !evicted) {
            SNT_LOG_WARN("Failed to evict released GPU mesh residencies during AssetManager shutdown: %s",
                         evicted.error().format().c_str());
        }
    }

    mesh_tokens_.clear();
    mesh_references_.clear();
    uploader_.shutdown();
    source_ = nullptr;
    device_ = nullptr;

    if (was_initialized) {
        SNT_LOG_INFO("AssetManager shut down");
    }
}

}  // namespace snt::assets
