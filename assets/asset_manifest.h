// AssetManifest: stable, declarative mapping of asset id -> path.
//
// Why a manifest?
//   - Runtime AssetCache assigns handle ids in load order, so the same
//     asset can get a different handle across runs (e.g. if a save file
//     references handle 3 but a new asset was added before it in the
//     load order, handle 3 now points to a different asset).
//   - The manifest fixes this by declaring assets in a stable order.
//     AssetManager::init_from_manifest() pre-allocates handles in the
//     exact manifest order, so handle N always refers to the same asset
//     declared at position N in the manifest.
//   - Scene files reference assets by handle (compact, no string lookup
//     at runtime), and the manifest makes those references stable.
//
// Format (JSON):
//   {
//     "assets": [
//       { "id": "cube",    "path": "test_assets/cube.obj" },
//       { "id": "sphere",  "path": "assets/sphere.obj" }
//     ]
//   }
//
// `id` is a stable, human-readable name (StringName-style). It is NOT
// the numeric handle — the handle is the array index, so the manifest
// author controls the order. The id is for humans + future asset
// refactoring (rename a path without changing the id).
//
// Layering: lives in assets/ so it can be loaded by AssetManager.
// Uses nlohmann_json (linked via snt_third_party).

#pragma once

#include "core/expected.h"

#include <string>
#include <vector>

namespace snt::assets {

// One entry in the manifest. `id` is the stable name; `path` is the
// asset file path (resolved via path_utils at load time).
struct AssetManifestEntry {
    std::string id;
    std::string path;
};

// Parsed manifest. The order of `entries` is significant: entry N gets
// handle N when registered with AssetCache::register_preallocated().
struct AssetManifest {
    std::vector<AssetManifestEntry> entries;
};

// Load a manifest from a JSON file.
//
// Behavior:
//   - File missing: returns an empty manifest (non-fatal; the engine
//     runs without pre-allocated assets, falling back to runtime load()).
//   - JSON parse error: returns an Error describing the parse failure.
//   - Duplicate ids: returns an Error (manifest is invalid).
//   - Duplicate paths: allowed (two ids can point to the same file —
//     useful for aliasing, e.g. "cube" and "default_mesh" both pointing
//     to cube.obj. They get DIFFERENT handles but load the same data).
//
// Example JSON (game/config/default_manifest.json):
//
//   {
//     "assets": [
//       { "id": "cube", "path": "test_assets/cube.obj" }
//     ]
//   }
snt::core::Expected<AssetManifest> load_manifest(const std::string& path);

}  // namespace snt::assets
