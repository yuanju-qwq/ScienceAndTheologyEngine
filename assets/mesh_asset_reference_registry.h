// Mesh asset reference boundary: stable scene-facing handles without GPU state.
//
// Ownership/lifecycle:
//   - IMeshAssetReferenceResolver maps source requests to stable MeshHandle
//     values and back for scene serialization. It owns no source bytes or
//     device resources.
//   - AssetManager implements this contract while also ensuring GPU residency.
//     MeshAssetReferenceRegistry is the source-free implementation used by
//     scene tools and tests that only need handle/path identity.
//   - All mutation is caller-synchronized. Runtime uses AssetManager only on
//     the render thread; offline tools use the registry on their main thread.

#pragma once

#include "assets/asset_handle.h"
#include "core/expected.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace snt::assets {

class IMeshAssetReferenceResolver {
public:
    virtual ~IMeshAssetReferenceResolver() = default;

    IMeshAssetReferenceResolver(const IMeshAssetReferenceResolver&) = delete;
    IMeshAssetReferenceResolver& operator=(const IMeshAssetReferenceResolver&) = delete;

    // Resolve a scene-facing request to a stable handle. Runtime resolvers may
    // make the corresponding mesh resident as part of this operation; callers
    // that only need asset identity can use MeshAssetReferenceRegistry.
    [[nodiscard]] virtual snt::core::Expected<MeshHandle> resolve_mesh(
        std::string requested_path) = 0;

    // Return the original source request for scene serialization. An invalid
    // or unknown handle maps to an empty string.
    [[nodiscard]] virtual std::string mesh_path(MeshHandle handle) const = 0;

protected:
    IMeshAssetReferenceResolver() = default;
};

// Source-free implementation for offline scene tools and focused tests. It
// intentionally has no GPU-residency behavior; the runtime AssetManager owns
// that policy through IGpuAssetUploader.
class MeshAssetReferenceRegistry final : public IMeshAssetReferenceResolver {
public:
    MeshAssetReferenceRegistry() = default;
    ~MeshAssetReferenceRegistry() override = default;

    MeshAssetReferenceRegistry(const MeshAssetReferenceRegistry&) = delete;
    MeshAssetReferenceRegistry& operator=(const MeshAssetReferenceRegistry&) = delete;

    [[nodiscard]] snt::core::Expected<MeshHandle> resolve_mesh(
        std::string requested_path) override;
    [[nodiscard]] std::string mesh_path(MeshHandle handle) const override;

    [[nodiscard]] std::optional<MeshHandle> find_mesh(
        std::string_view requested_path) const;
    [[nodiscard]] std::size_t size() const noexcept;
    void clear() noexcept;

private:
    std::unordered_map<std::string, MeshHandle> handles_by_path_;
    std::vector<std::string> paths_by_handle_;
};

}  // namespace snt::assets
