// Mesh asset reference registry implementation.

#include "assets/mesh_asset_reference_registry.h"

#include <cstdint>
#include <utility>

namespace snt::assets {

snt::core::Expected<MeshHandle> MeshAssetReferenceRegistry::resolve_mesh(
    std::string requested_path) {
    if (requested_path.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "MeshAssetReferenceRegistry::resolve_mesh: empty path"};
    }

    if (const auto existing = handles_by_path_.find(requested_path);
        existing != handles_by_path_.end()) {
        return existing->second;
    }

    if (paths_by_handle_.size() >= MeshHandle::kInvalidId) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "MeshAssetReferenceRegistry::resolve_mesh: handle space exhausted"};
    }

    const MeshHandle handle{static_cast<std::uint32_t>(paths_by_handle_.size())};
    paths_by_handle_.push_back(std::move(requested_path));
    handles_by_path_.emplace(paths_by_handle_.back(), handle);
    return handle;
}

std::string MeshAssetReferenceRegistry::mesh_path(MeshHandle handle) const {
    if (!handle.valid() || handle.id >= paths_by_handle_.size()) {
        return {};
    }
    return paths_by_handle_[handle.id];
}

std::optional<MeshHandle> MeshAssetReferenceRegistry::find_mesh(
    std::string_view requested_path) const {
    const auto found = handles_by_path_.find(std::string(requested_path));
    if (found == handles_by_path_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::size_t MeshAssetReferenceRegistry::size() const noexcept {
    return paths_by_handle_.size();
}

void MeshAssetReferenceRegistry::clear() noexcept {
    handles_by_path_.clear();
    paths_by_handle_.clear();
}

}  // namespace snt::assets
