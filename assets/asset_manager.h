// AssetManager: central registry for asset caches.
//
// Design:
//   - Runtime-owned service. Consumers receive an explicit reference through
//     RuntimeServices or their constructor/setter dependency boundary.
//   - init(VulkanDevice*, IAssetSource&, AssetCatalog) wires up the built-in
//     caches (currently mesh) from the Runtime-owned content boundary.
//     New asset types register here: add a VulkanTextureLoader, an
//     AssetCache<VulkanTexture>, and a texture_cache() accessor.
//   - shutdown() releases every cache. Must be called BEFORE the
//     VulkanDevice it was initialized with is destroyed.
//
// Future:
//   - Hot-reload: a watch thread on asset dirs triggers reload(handle).
//   - Async loading: load_async() submits a JobSystem task + returns
//     Future<Handle>.

#pragma once

#include "assets/asset_cache.h"
#include "assets/asset_catalog.h"
#include "assets/asset_handle.h"
#include "assets/asset_source.h"
#include "assets/mesh_asset_loader.h"
#include "core/expected.h"

namespace snt::render_backend {
class VulkanDevice;
class VulkanMesh;
}

namespace snt::assets {

class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager() { shutdown(); }

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Initialize all built-in GPU caches from an immutable catalog. All
    // dependencies are borrowed: Runtime owns the source/catalog and the
    // VulkanDevice must outlive this manager (call shutdown() before
    // destroying the device). Each mesh cache miss reads owned source bytes
    // through source before the legacy VulkanMeshLoader decodes them.
    //
    // This is render-thread/device-affine. IAssetSource itself may be called
    // by workers elsewhere, but this manager invokes it only while creating
    // or eagerly loading GPU resources on the render thread.
    snt::core::Expected<void> init(
        snt::render_backend::VulkanDevice* device,
        IAssetSource& source,
        const AssetCatalog& catalog);

    // Release all caches. Idempotent. Must be called before the
    // VulkanDevice passed to init() is destroyed.
    void shutdown();

    // Access the mesh cache. Engine loads via this; RenderSystem resolves
    // handles via this. Returns a reference valid until shutdown().
    // Uses MeshAssetTag so handles are AssetHandle<MeshAssetTag> (= MeshHandle),
    // decoupled from the VulkanMesh type.
    AssetCache<snt::render_backend::VulkanMesh, MeshAssetTag>& mesh_cache() { return mesh_cache_; }

private:
    // Wires up the legacy mesh cache after device_ and source_ are set.
    snt::core::Expected<void> init_mesh_cache();

    snt::render_backend::VulkanDevice* device_ = nullptr;
    IAssetSource* source_ = nullptr;

    // Built-in loaders (own no heap state beyond the borrowed device ptr).
    VulkanMeshLoader mesh_loader_;

    // Built-in caches.
    AssetCache<snt::render_backend::VulkanMesh, MeshAssetTag> mesh_cache_;
};

}  // namespace snt::assets
