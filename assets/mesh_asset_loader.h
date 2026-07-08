// VulkanMesh asset loader: bridges the asset system to VulkanMesh.
//
// Used by AssetManager to wire AssetCache<VulkanMesh>:
//   - load(path)   -> new VulkanMesh + load_obj()
//   - destroy(m)   -> m->destroy() + delete m
//
// Default color is applied to all vertices (P1.5 behavior; P3 will
// replace with a material system).

#pragma once

#include "core/expected.h"

#include <string>

namespace snt::render_backend {
class VulkanDevice;
class VulkanMesh;
}

namespace snt::assets {

class VulkanMeshLoader {
public:
    VulkanMeshLoader() = default;
    ~VulkanMeshLoader() = default;

    VulkanMeshLoader(const VulkanMeshLoader&) = delete;
    VulkanMeshLoader& operator=(const VulkanMeshLoader&) = delete;

    // Bind to a Vulkan device. The device is borrowed (not owned) and
    // must outlive the loader.
    void init(snt::render_backend::VulkanDevice* device) { device_ = device; }

    // Load a .obj file. Returns a heap-owned VulkanMesh* on success
    // (caller takes ownership) or an Error on failure.
    snt::core::Expected<snt::render_backend::VulkanMesh*> load(const std::string& path) const;

    // Release a previously-loaded mesh (calls mesh->destroy() + delete).
    // Safe to call with nullptr (no-op).
    void destroy(snt::render_backend::VulkanMesh* mesh) const;

private:
    snt::render_backend::VulkanDevice* device_ = nullptr;
};

}  // namespace snt::assets
