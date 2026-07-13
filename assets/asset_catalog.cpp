// AssetCatalog implementation.

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/asset_catalog.h"

#include <string>
#include <utility>

namespace snt::assets {

AssetCatalog::AssetCatalog(
    AssetManifest manifest,
    std::string manifest_identity,
    std::unordered_map<std::string, std::size_t> index_by_id)
    : manifest_(std::move(manifest)),
      manifest_identity_(std::move(manifest_identity)),
      index_by_id_(std::move(index_by_id)) {}

snt::core::Expected<AssetCatalog> AssetCatalog::from_manifest(
    AssetManifest manifest,
    std::string manifest_identity) {
    std::unordered_map<std::string, std::size_t> index_by_id;
    index_by_id.reserve(manifest.entries.size());
    for (std::size_t index = 0; index < manifest.entries.size(); ++index) {
        const auto& entry = manifest.entries[index];
        if (!index_by_id.emplace(entry.id, index).second) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidArgument,
                "AssetCatalog: duplicate asset id '" + entry.id + "'"};
        }
    }
    return AssetCatalog(std::move(manifest),
                        std::move(manifest_identity),
                        std::move(index_by_id));
}

snt::core::Expected<AssetCatalog> AssetCatalog::load(
    IAssetSource& source,
    AssetSourceRequest manifest_request) {
    const std::string requested_manifest_path = manifest_request.requested_path;
    auto source_data = source.read(std::move(manifest_request));
    if (!source_data) {
        if (source_data.error().code() == snt::core::ErrorCode::kFileNotFound) {
            SNT_LOG_INFO("Asset catalog manifest '%s' not found; using an empty catalog",
                         requested_manifest_path.c_str());
            return from_manifest(AssetManifest{}, requested_manifest_path);
        }
        snt::core::Error error = source_data.error();
        error.with_context("AssetCatalog::load('" + requested_manifest_path + "')");
        return error;
    }

    const std::string manifest_text(source_data->bytes.begin(),
                                    source_data->bytes.end());
    auto manifest = parse_manifest(source_data->canonical_path, manifest_text);
    if (!manifest) {
        snt::core::Error error = manifest.error();
        error.with_context("AssetCatalog::load");
        return error;
    }

    auto catalog = from_manifest(std::move(*manifest), source_data->canonical_path);
    if (!catalog) {
        return catalog.error();
    }
    SNT_LOG_INFO("Asset catalog loaded from '%s' (%zu entries)",
                 catalog->manifest_identity().c_str(), catalog->size());
    return catalog;
}

snt::core::Expected<AssetSourceRequest> AssetCatalog::resolve(
    std::string_view asset_id) const {
    if (asset_id.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "AssetCatalog::resolve: empty asset id"};
    }
    const auto found = index_by_id_.find(std::string(asset_id));
    if (found == index_by_id_.end()) {
        return snt::core::Error{
            snt::core::ErrorCode::kAssetNotFound,
            "AssetCatalog::resolve: unknown asset id '" + std::string(asset_id) + "'"};
    }

    AssetSourceRequest request;
    request.requested_path = manifest_.entries[found->second].path;
    return request;
}

const AssetManifest& AssetCatalog::manifest() const noexcept {
    return manifest_;
}

const std::string& AssetCatalog::manifest_identity() const noexcept {
    return manifest_identity_;
}

std::size_t AssetCatalog::size() const noexcept {
    return manifest_.entries.size();
}

}  // namespace snt::assets
