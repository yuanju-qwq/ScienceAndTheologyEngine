// VulkanMesh asset loader implementation.

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/mesh_asset_loader.h"

#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_mesh.h"

#include <format>

namespace snt::assets {

snt::core::Expected<snt::render_backend::VulkanMesh*> VulkanMeshLoader::load(const std::string& path) const {
    if (!device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "VulkanMeshLoader::load: device not set"};
    }
    auto* mesh = new snt::render_backend::VulkanMesh();
    // Default color — P3 will replace with material system.
    float default_color[3] = {1.0f, 0.5f, 0.2f};
    if (auto r = mesh->load_obj(*device_, path, default_color); !r) {
        delete mesh;
        snt::core::Error e = r.error();
        e.with_context(std::format("VulkanMeshLoader::load('{}')", path));
        return e;
    }
    return mesh;
}

void VulkanMeshLoader::destroy(snt::render_backend::VulkanMesh* mesh) const {
    if (!mesh) return;
    mesh->destroy();
    delete mesh;
}

}  // namespace snt::assets
