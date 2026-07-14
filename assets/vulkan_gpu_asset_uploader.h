// Vulkan GPU asset uploader: mesh residency behind the IGpuAssetUploader API.
//
// Ownership/lifecycle:
//   - The uploader borrows VulkanDevice and is render-thread/device-affine.
//   - upload() creates one logical token per caller. Meshes with the same
//     canonical source identity share one VulkanMesh allocation.
//   - release() drops a logical token. evict_unused() performs deferred
//     physical destruction only after the caller has synchronized GPU work.
//   - shutdown() invalidates every token and must run before VulkanDevice is
//     destroyed. AssetManager owns this order in the runtime.

#pragma once

#include "assets/gpu_asset_uploader.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace snt::render_backend {
class VulkanDevice;
class VulkanMesh;
}

namespace snt::assets {

class VulkanGpuAssetUploader final : public IGpuAssetUploader {
public:
    VulkanGpuAssetUploader() = default;
    ~VulkanGpuAssetUploader() override;

    VulkanGpuAssetUploader(const VulkanGpuAssetUploader&) = delete;
    VulkanGpuAssetUploader& operator=(const VulkanGpuAssetUploader&) = delete;

    [[nodiscard]] snt::core::Expected<void> init(
        snt::render_backend::VulkanDevice* device);
    void shutdown();

    [[nodiscard]] snt::core::Expected<GpuAssetResidencyToken> upload(
        GpuAssetUploadRequest request) override;
    [[nodiscard]] snt::core::Expected<void> release(
        GpuAssetResidencyToken token) override;
    [[nodiscard]] snt::core::Expected<std::size_t> evict_unused() override;

    // Backend-only access for AssetManager. The public uploader interface
    // remains Vulkan-free; callers should retain MeshHandle rather than this
    // pointer and resolve it through AssetManager at draw time.
    [[nodiscard]] snt::render_backend::VulkanMesh* mesh(
        GpuAssetResidencyToken token) const noexcept;
    [[nodiscard]] std::size_t resident_count() const noexcept;

private:
    struct MeshResidency {
        snt::render_backend::VulkanMesh* mesh = nullptr;
        std::size_t lease_count = 0;
    };

    [[nodiscard]] snt::core::Expected<GpuAssetResidencyToken> allocate_token(
        const std::string& canonical_path);
    void destroy_mesh(snt::render_backend::VulkanMesh* mesh) const;

    snt::render_backend::VulkanDevice* device_ = nullptr;
    std::uint64_t next_token_value_ = 1;
    std::unordered_map<std::string, MeshResidency> meshes_by_path_;
    std::unordered_map<std::uint64_t, std::string> paths_by_token_;
};

}  // namespace snt::assets
