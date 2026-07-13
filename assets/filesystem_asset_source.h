// Filesystem asset source: concrete IAssetSource for one owned content root.
//
// Ownership/lifecycle:
//   - The source owns a canonical directory root and is independent of a
//     RuntimePathResolver after create(). Hosts may pass any explicitly owned
//     package root (normally RuntimePaths::game_root).
//   - read() has no mutable state and is safe to call from loading workers.
//     It only returns owned bytes; it never accesses GPU state, World, or a
//     process-global service.
//   - Every request must be relative to root(). Rooted, traversal, and
//     resolved symlink escapes are rejected before file bytes are loaded.
//
// AssetManager does not use this source yet. It remains a staged catalog/source
// implementation until GPU residency migrates behind IGpuAssetUploader.

#pragma once

#include "assets/asset_source.h"

#include <filesystem>

namespace snt::assets {

class FilesystemAssetSource final : public IAssetSource {
public:
    // Validate and canonicalize an existing content directory. The returned
    // source owns that canonical root, so its caller may release the original
    // path/resolver value immediately after create() succeeds.
    [[nodiscard]] static snt::core::Expected<FilesystemAssetSource> create(
        std::filesystem::path root);

    FilesystemAssetSource(FilesystemAssetSource&&) noexcept = default;
    FilesystemAssetSource& operator=(FilesystemAssetSource&&) noexcept = default;

    [[nodiscard]] const std::filesystem::path& root() const noexcept;

    // Resolve a relative request below root(), reject root escapes, and return
    // owned bytes plus the canonical source identity.
    [[nodiscard]] snt::core::Expected<AssetSourceData> read(
        AssetSourceRequest request) override;

private:
    explicit FilesystemAssetSource(std::filesystem::path root);

    std::filesystem::path root_;
};

}  // namespace snt::assets
