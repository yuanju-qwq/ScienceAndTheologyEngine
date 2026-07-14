// Vulkan GPU asset uploader implementation.

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/vulkan_gpu_asset_uploader.h"

#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_mesh.h"

namespace snt::assets {

VulkanGpuAssetUploader::~VulkanGpuAssetUploader() {
    shutdown();
}

snt::core::Expected<void> VulkanGpuAssetUploader::init(
    snt::render_backend::VulkanDevice* device) {
    if (!device) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "VulkanGpuAssetUploader::init: null device"};
    }
    if (device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "VulkanGpuAssetUploader::init: already initialized"};
    }

    device_ = device;
    SNT_LOG_INFO("Vulkan GPU asset uploader initialized (device=%p)",
                 static_cast<const void*>(device_));
    return {};
}

snt::core::Expected<GpuAssetResidencyToken> VulkanGpuAssetUploader::upload(
    GpuAssetUploadRequest request) {
    if (!device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "VulkanGpuAssetUploader::upload: not initialized"};
    }
    if (request.kind != GpuAssetKind::kMesh) {
        return snt::core::Error{snt::core::ErrorCode::kNotImplemented,
                                "VulkanGpuAssetUploader::upload: only mesh assets are implemented"};
    }
    if (request.source.canonical_path.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "VulkanGpuAssetUploader::upload: source has no canonical identity"};
    }

    const std::string canonical_path = request.source.canonical_path;
    auto residency = meshes_by_path_.find(canonical_path);
    const bool created_residency = residency == meshes_by_path_.end();
    if (created_residency) {
        auto* mesh = new snt::render_backend::VulkanMesh();
        const float default_color[3] = {1.0f, 0.5f, 0.2f};
        if (auto result = mesh->load_obj(*device_, request.source.canonical_path,
                                         request.source.bytes, default_color);
            !result) {
            delete mesh;
            snt::core::Error error = result.error();
            error.with_context("VulkanGpuAssetUploader::upload('" + canonical_path + "')");
            return error;
        }

        residency = meshes_by_path_.emplace(
            canonical_path, MeshResidency{.mesh = mesh, .lease_count = 0}).first;
        SNT_LOG_INFO("GPU mesh resident: '%s' (%u vertices, %u indices)",
                     canonical_path.c_str(), mesh->vertex_count(), mesh->index_count());
    } else {
        SNT_LOG_DEBUG("GPU mesh residency reused: '%s'", canonical_path.c_str());
    }

    auto token = allocate_token(canonical_path);
    if (!token) {
        if (created_residency) {
            destroy_mesh(residency->second.mesh);
            meshes_by_path_.erase(residency);
        }
        return token.error();
    }
    ++residency->second.lease_count;
    return *token;
}

snt::core::Expected<void> VulkanGpuAssetUploader::release(
    GpuAssetResidencyToken token) {
    if (!device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "VulkanGpuAssetUploader::release: not initialized"};
    }
    if (!token.valid()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "VulkanGpuAssetUploader::release: invalid token"};
    }

    const auto token_it = paths_by_token_.find(token.value);
    if (token_it == paths_by_token_.end()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "VulkanGpuAssetUploader::release: unknown or released token"};
    }
    const auto residency = meshes_by_path_.find(token_it->second);
    if (residency == meshes_by_path_.end() || residency->second.lease_count == 0) {
        SNT_LOG_ERROR("GPU asset uploader state mismatch while releasing token %llu",
                      static_cast<unsigned long long>(token.value));
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "VulkanGpuAssetUploader::release: residency state mismatch"};
    }

    --residency->second.lease_count;
    paths_by_token_.erase(token_it);
    return {};
}

snt::core::Expected<std::size_t> VulkanGpuAssetUploader::evict_unused() {
    if (!device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "VulkanGpuAssetUploader::evict_unused: not initialized"};
    }

    std::size_t reclaimed = 0;
    for (auto it = meshes_by_path_.begin(); it != meshes_by_path_.end();) {
        if (it->second.lease_count != 0) {
            ++it;
            continue;
        }
        destroy_mesh(it->second.mesh);
        it = meshes_by_path_.erase(it);
        ++reclaimed;
    }
    if (reclaimed != 0) {
        SNT_LOG_INFO("GPU asset uploader evicted %zu released mesh residencies", reclaimed);
    }
    return reclaimed;
}

snt::render_backend::VulkanMesh* VulkanGpuAssetUploader::mesh(
    GpuAssetResidencyToken token) const noexcept {
    const auto token_it = paths_by_token_.find(token.value);
    if (token_it == paths_by_token_.end()) {
        return nullptr;
    }
    const auto residency = meshes_by_path_.find(token_it->second);
    return residency != meshes_by_path_.end() ? residency->second.mesh : nullptr;
}

std::size_t VulkanGpuAssetUploader::resident_count() const noexcept {
    return meshes_by_path_.size();
}

snt::core::Expected<GpuAssetResidencyToken> VulkanGpuAssetUploader::allocate_token(
    const std::string& canonical_path) {
    if (next_token_value_ == GpuAssetResidencyToken::kInvalidValue) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "VulkanGpuAssetUploader::upload: token space exhausted"};
    }

    const GpuAssetResidencyToken token{next_token_value_++};
    paths_by_token_.emplace(token.value, canonical_path);
    return token;
}

void VulkanGpuAssetUploader::destroy_mesh(
    snt::render_backend::VulkanMesh* mesh) const {
    delete mesh;
}

void VulkanGpuAssetUploader::shutdown() {
    if (!device_ && meshes_by_path_.empty() && paths_by_token_.empty()) {
        return;
    }

    const std::size_t resident_meshes = meshes_by_path_.size();
    const std::size_t active_tokens = paths_by_token_.size();
    for (auto& [canonical_path, residency] : meshes_by_path_) {
        (void)canonical_path;
        destroy_mesh(residency.mesh);
    }
    meshes_by_path_.clear();
    paths_by_token_.clear();
    device_ = nullptr;
    // Keep token values monotonic across device lifetimes. Resetting this
    // counter would let a stale pre-shutdown token release a new residency
    // after the uploader is initialized again.

    SNT_LOG_INFO("Vulkan GPU asset uploader shut down (%zu meshes, %zu outstanding tokens invalidated)",
                 resident_meshes, active_tokens);
}

}  // namespace snt::assets
