// FilesystemAssetSource implementation.

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/filesystem_asset_source.h"
#include "assets/loader.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace snt::assets {
namespace {

namespace fs = std::filesystem;

bool is_at_or_below(const fs::path& candidate, const fs::path& root) {
    auto candidate_it = candidate.begin();
    for (auto root_it = root.begin(); root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *candidate_it != *root_it) {
            return false;
        }
    }
    return true;
}

snt::core::Error filesystem_error(
    const char* operation,
    const fs::path& path,
    const std::error_code& error) {
    return snt::core::Error{
        snt::core::ErrorCode::kAssetLoadFailed,
        std::string("FilesystemAssetSource ") + operation + " '" +
            path.generic_string() + "': " + error.message()};
}

}  // namespace

FilesystemAssetSource::FilesystemAssetSource(fs::path root)
    : root_(std::move(root)) {}

snt::core::Expected<FilesystemAssetSource> FilesystemAssetSource::create(
    fs::path root) {
    if (root.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "FilesystemAssetSource::create: empty root"};
    }

    std::error_code error;
    const fs::path absolute_root = fs::absolute(root, error);
    if (error) {
        return filesystem_error("could not normalize root", root, error);
    }
    if (!fs::exists(absolute_root, error)) {
        if (error) {
            return filesystem_error("could not inspect root", absolute_root, error);
        }
        return snt::core::Error{
            snt::core::ErrorCode::kFileNotFound,
            "FilesystemAssetSource::create: root does not exist: " +
                absolute_root.generic_string()};
    }
    if (!fs::is_directory(absolute_root, error)) {
        if (error) {
            return filesystem_error("could not inspect root", absolute_root, error);
        }
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "FilesystemAssetSource::create: root is not a directory: " +
                absolute_root.generic_string()};
    }

    const fs::path canonical_root = fs::canonical(absolute_root, error);
    if (error) {
        return filesystem_error("could not canonicalize root", absolute_root, error);
    }

    const std::string root_identity = canonical_root.generic_string();
    SNT_LOG_INFO("Filesystem asset source created (root='%s')", root_identity.c_str());
    return FilesystemAssetSource(canonical_root);
}

const fs::path& FilesystemAssetSource::root() const noexcept {
    return root_;
}

snt::core::Expected<AssetSourceData> FilesystemAssetSource::read(
    AssetSourceRequest request) {
    if (request.requested_path.empty()) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "FilesystemAssetSource::read: empty requested path"};
    }

    const fs::path requested_path(request.requested_path);
    if (requested_path.is_absolute() || requested_path.has_root_name() ||
        requested_path.has_root_directory()) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "FilesystemAssetSource::read: rooted paths are not allowed: " +
                request.requested_path};
    }

    std::error_code error;
    const fs::path candidate = (root_ / requested_path).lexically_normal();
    if (!is_at_or_below(candidate, root_)) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "FilesystemAssetSource::read: requested path escapes root: " +
                request.requested_path};
    }
    if (!fs::exists(candidate, error)) {
        if (error) {
            return filesystem_error("could not inspect asset", candidate, error);
        }
        return snt::core::Error{
            snt::core::ErrorCode::kFileNotFound,
            "FilesystemAssetSource::read: asset does not exist: " +
                candidate.generic_string()};
    }

    const fs::path canonical_path = fs::canonical(candidate, error);
    if (error) {
        return filesystem_error("could not canonicalize asset", candidate, error);
    }
    if (!is_at_or_below(canonical_path, root_)) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "FilesystemAssetSource::read: resolved asset escapes root: " +
                request.requested_path};
    }
    if (!fs::is_regular_file(canonical_path, error)) {
        if (error) {
            return filesystem_error("could not inspect asset", canonical_path, error);
        }
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "FilesystemAssetSource::read: asset is not a regular file: " +
                canonical_path.generic_string()};
    }

    auto bytes = load_binary_file(canonical_path.string());
    if (!bytes) {
        snt::core::Error asset_error = bytes.error();
        asset_error.with_context(
            "FilesystemAssetSource::read('" + request.requested_path + "')");
        return asset_error;
    }

    AssetSourceData result;
    result.canonical_path = canonical_path.generic_string();
    result.bytes = std::move(*bytes);
    return result;
}

}  // namespace snt::assets
