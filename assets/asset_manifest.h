// AssetManifest: stable, declarative mapping of asset id -> path.
//
// Why a manifest?
//   - Runtime AssetCache assigns handle ids in load order, so the same
//     asset can get a different handle across runs (e.g. if a save file
//     references handle 3 but a new asset was added before it in the
//     load order, handle 3 now points to a different asset).
//   - AssetCatalog owns source-backed manifest loading and preserves its
//     declared order. Runtime injects that immutable catalog into
//     AssetManager before a game session creates its World.
//   - Scene files reference assets by handle (compact, no string lookup
//     at runtime), and the manifest makes those references stable.
//
// Format (JSON):
//   {
//     "assets": [
//       { "id": "cube",    "path": "assets/dev/cube.obj" },
//       { "id": "sphere",  "path": "assets/sphere.obj" }
//     ]
//   }
//
// `id` is a stable, human-readable name (StringName-style). It is NOT
// the numeric handle — the handle is the array index, so the manifest
// author controls the order. The id is for humans + future asset
// refactoring (rename a path without changing the id).
//
// Layering: this header owns only the manifest value format and pure parser.
// Source I/O belongs to AssetCatalog/IAssetSource. Uses nlohmann_json in its
// implementation (linked via snt_third_party).

#pragma once

#include "core/expected.h"

#include <string>
#include <string_view>
#include <vector>

namespace snt::assets {

// One entry in the manifest. `id` is the stable name; `path` is a
// source-relative request resolved only by the injected IAssetSource.
struct AssetManifestEntry {
    std::string id;
    std::string path;
};

// Parsed manifest. The order of `entries` is significant: entry N gets
// handle N when registered with AssetCache::register_preallocated().
struct AssetManifest {
    std::vector<AssetManifestEntry> entries;
};

// Parse one manifest from already-owned JSON text. `source_identity` is used
// only in error context, so this function is independent of filesystem,
// package, and network transports. It does not log; callers choose the final
// loading boundary that owns diagnostics.
[[nodiscard]] snt::core::Expected<AssetManifest> parse_manifest(
    std::string_view source_identity,
    std::string_view text);

}  // namespace snt::assets
