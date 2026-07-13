// AssetManifest JSON loader implementation.

#include "assets/asset_manifest.h"

#include <nlohmann/json.hpp>

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

snt::core::Expected<AssetManifest> parse_manifest(
    std::string_view source_identity,
    std::string_view text) {
    const std::string source_label = source_identity.empty()
        ? "<unknown>"
        : std::string(source_identity);

    json j;
    try {
        j = json::parse(text.begin(), text.end());
    } catch (const std::exception& e) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "manifest JSON parse error in '" + source_label + "': " + e.what()};
    }

    AssetManifest manifest;
    if (!j.contains("assets") || !j["assets"].is_array()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "manifest '" + source_label + "' missing 'assets' array"};
    }

    // Track ids to detect duplicates. Paths are allowed to repeat (aliasing).
    std::unordered_set<std::string> seen_ids;

    for (const auto& entry_json : j["assets"]) {
        auto entry = parse_entry(entry_json);
        if (!entry) {
            snt::core::Error error = entry.error();
            error.with_context("manifest '" + source_label + "', entry #" +
                               std::to_string(manifest.entries.size()));
            return error;
        }
        if (!seen_ids.insert(entry->id).second) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidArgument,
                "manifest '" + source_label + "' has duplicate id '" +
                    entry->id + "'"};
        }
        manifest.entries.push_back(*entry);
    }

    return manifest;
}

}  // namespace snt::assets
