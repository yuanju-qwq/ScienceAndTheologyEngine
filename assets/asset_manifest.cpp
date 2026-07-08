// AssetManifest JSON loader implementation.

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/asset_manifest.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <unordered_set>

namespace snt::assets {

namespace {

using json = nlohmann::json;

// Parse one entry from a JSON object. Validates that both `id` and
// `path` are present and non-empty strings.
snt::core::Expected<AssetManifestEntry> parse_entry(const json& j) {
    AssetManifestEntry e;
    if (!j.contains("id") || !j["id"].is_string()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "manifest entry missing string field 'id'"};
    }
    if (!j.contains("path") || !j["path"].is_string()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "manifest entry missing string field 'path'"};
    }
    e.id = j["id"].get<std::string>();
    e.path = j["path"].get<std::string>();
    if (e.id.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "manifest entry has empty 'id'"};
    }
    if (e.path.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "manifest entry has empty 'path'"};
    }
    return e;
}

}  // namespace

snt::core::Expected<AssetManifest> load_manifest(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        // Missing manifest is non-fatal: return an empty manifest so
        // the engine runs without pre-allocated assets.
        SNT_LOG_INFO("Asset manifest '%s' not found; skipping pre-allocation",
                     path.c_str());
        return AssetManifest{};
    }

    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string text = ss.str();

    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                               std::string("manifest JSON parse error: ") + e.what()};
    }

    AssetManifest manifest;
    if (!j.contains("assets") || !j["assets"].is_array()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "manifest missing 'assets' array"};
    }

    // Track ids to detect duplicates. Paths are allowed to repeat (aliasing).
    std::unordered_set<std::string> seen_ids;

    for (const auto& entry_json : j["assets"]) {
        auto r = parse_entry(entry_json);
        if (!r) {
            snt::core::Error e = r.error();
            e.with_context("manifest entry #" + std::to_string(manifest.entries.size()));
            return e;
        }
        if (!seen_ids.insert(r->id).second) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                   "manifest has duplicate id '" + r->id + "'"};
        }
        manifest.entries.push_back(*r);
    }

    SNT_LOG_INFO("Asset manifest loaded from '%s' (%zu entries)",
                 path.c_str(), manifest.entries.size());
    return manifest;
}

}  // namespace snt::assets
