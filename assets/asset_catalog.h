// Asset catalog: immutable logical asset-id to source-request mapping.
//
// Ownership/lifecycle:
//   - AssetCatalog owns the parsed manifest, its canonical manifest identity,
//     and its lookup index. It does not own IAssetSource or GPU residency.
//   - load() may run on a loading worker if its injected IAssetSource supports
//     that thread. After load succeeds, resolve() is read-only and safe for
//     concurrent callers.
//   - The catalog exposes only value requests. It cannot mutate World, access
//     a render device, or recover services from process-global state.
//
// This is the catalog/source half of the asset boundary. AssetManager consumes
// it before game content registration and creates uploader-backed mesh residency.

#pragma once

#include "assets/asset_manifest.h"
#include "assets/asset_source.h"
#include "core/expected.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace snt::assets {

class AssetCatalog {
public:
    // Read and parse a JSON manifest through an explicit source. A missing
    // manifest remains non-fatal and produces an empty catalog, matching the
    // current AssetManager fallback behavior.
    [[nodiscard]] static snt::core::Expected<AssetCatalog> load(
        IAssetSource& source,
        AssetSourceRequest manifest_request);

    AssetCatalog(const AssetCatalog&) = delete;
    AssetCatalog& operator=(const AssetCatalog&) = delete;
    AssetCatalog(AssetCatalog&&) noexcept = default;
    AssetCatalog& operator=(AssetCatalog&&) noexcept = default;

    // Resolve a stable manifest id to a value request for the same source that
    // loaded the manifest. No file read or GPU work happens in this call.
    [[nodiscard]] snt::core::Expected<AssetSourceRequest> resolve(
        std::string_view asset_id) const;

    [[nodiscard]] const AssetManifest& manifest() const noexcept;
    [[nodiscard]] const std::string& manifest_identity() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    AssetCatalog(AssetManifest manifest,
                 std::string manifest_identity,
                 std::unordered_map<std::string, std::size_t> index_by_id);

    [[nodiscard]] static snt::core::Expected<AssetCatalog> from_manifest(
        AssetManifest manifest,
        std::string manifest_identity);

    AssetManifest manifest_;
    std::string manifest_identity_;
    std::unordered_map<std::string, std::size_t> index_by_id_;
};

}  // namespace snt::assets
