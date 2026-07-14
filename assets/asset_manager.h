// AssetManager: runtime-facing mesh identity and GPU residency service.
//
// Design:
//   - ClientRuntime owns this service; SimulationRuntime never exposes GPU
//     residency to a headless session.
//   - MeshHandle/path identity is kept separate from Vulkan residency:
//     MeshAssetReferenceRegistry owns the former, VulkanGpuAssetUploader owns
//     the latter through IGpuAssetUploader token leases.
//   - init() and resolve_mesh() are render-thread/device-affine because they
//     may upload resources. IAssetSource itself remains worker-capable.
//   - shutdown() releases every lease, evicts released resources, and runs
//     before the borrowed VulkanDevice is destroyed.
//
// Future:
//   - Texture/shader uploaders use the same IGpuAssetUploader contract.
//   - Hot reload can replace a token after GPU synchronization while retaining
//     the stable scene-facing MeshHandle.

#pragma once

#include "assets/asset_catalog.h"
#include "assets/asset_source.h"
#include "assets/mesh_asset_reference_registry.h"
#include "assets/vulkan_gpu_asset_uploader.h"
#include "core/expected.h"

#include <cstddef>
#include <string>
#include <vector>

namespace snt::render_backend {
class VulkanDevice;
class VulkanMesh;
}

namespace snt::assets {

class AssetManager final : public IMeshAssetReferenceResolver {
public:
    AssetManager() = default;
    ~AssetManager() { shutdown(); }

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Initialize mesh residency from an immutable source-backed catalog. The
    // source and device are borrowed and must outlive this manager; catalog
    // entries are eagerly uploaded in manifest order before the first frame.
    [[nodiscard]] snt::core::Expected<void> init(
        snt::render_backend::VulkanDevice* device,
        IAssetSource& source,
        const AssetCatalog& catalog);

    // Resolve a source-relative scene mesh request. A new request is read from
    // IAssetSource and handed by value to IGpuAssetUploader; an existing one
    // returns its stable handle without duplicate GPU allocation.
    [[nodiscard]] snt::core::Expected<MeshHandle> resolve_mesh(
        std::string requested_path) override;
    [[nodiscard]] std::string mesh_path(MeshHandle handle) const override;

    // Resolve a stable handle to its currently resident Vulkan mesh. The
    // returned pointer is borrowed and valid until shutdown() or future reload.
    [[nodiscard]] snt::render_backend::VulkanMesh* mesh(
        MeshHandle handle) const noexcept;
    [[nodiscard]] std::size_t mesh_count() const noexcept;

    // Idempotently release every token and GPU residency. Must be called before
    // the VulkanDevice passed to init() is destroyed.
    void shutdown();

private:
    snt::render_backend::VulkanDevice* device_ = nullptr;
    IAssetSource* source_ = nullptr;
    MeshAssetReferenceRegistry mesh_references_;
    std::vector<GpuAssetResidencyToken> mesh_tokens_;
    VulkanGpuAssetUploader uploader_;
};

}  // namespace snt::assets
