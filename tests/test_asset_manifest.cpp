// Unit tests for AssetManifest + AssetCache::register_preallocated / path_of.
//
// Phase 2: validates that manifest loading + pre-allocation produces
// stable handles (same manifest -> same handles across runs) and that
// path_of() reverse-lookups work for serialization.

#include "assets/asset_cache.h"
#include "assets/asset_handle.h"
#include "assets/asset_manifest.h"

#include <gtest/gtest.h>

#include <fstream>
#include <filesystem>

using snt::assets::AssetCache;
using snt::assets::AssetHandle;
using snt::assets::AssetManifest;
using snt::assets::AssetManifestEntry;
using snt::assets::load_manifest;
using snt::core::Expected;

namespace {

// Phantom tag + dummy asset for cache tests (mirrors test_asset_cache.cpp).
struct ManifestFakeAsset {
    int value = 0;
};

// Helper: write a temporary JSON file and return its path.
std::string write_temp_json(const std::string& name, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream ofs(path);
    ofs << contents;
    ofs.close();
    return path.string();
}

}  // namespace

// ===========================================================================
// load_manifest
// ===========================================================================

TEST(ManifestTest, MissingFileReturnsEmptyManifest) {
    auto result = load_manifest("definitely_does_not_exist_manifest.json");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->entries.empty());
}

TEST(ManifestTest, ParseErrorReturnsError) {
    const std::string bad_json = "{ this is not valid json ";
    const auto path = write_temp_json("snt_test_manifest_bad.json", bad_json);

    auto result = load_manifest(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("parse"), std::string::npos);
}

TEST(ManifestTest, EmptyAssetsArrayIsAccepted) {
    const std::string json = R"({ "assets": [] })";
    const auto path = write_temp_json("snt_test_manifest_empty.json", json);

    auto result = load_manifest(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->entries.empty());
}

TEST(ManifestTest, MissingAssetsArrayReturnsError) {
    const std::string json = R"({ "not_assets": [] })";
    const auto path = write_temp_json("snt_test_manifest_no_array.json", json);

    auto result = load_manifest(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("assets"), std::string::npos);
}

TEST(ManifestTest, ParsesEntriesInOrder) {
    const std::string json = R"({
        "assets": [
            { "id": "cube",    "path": "assets/cube.obj" },
            { "id": "sphere",  "path": "assets/sphere.obj" },
            { "id": "pyramid", "path": "assets/pyramid.obj" }
        ]
    })";
    const auto path = write_temp_json("snt_test_manifest_ok.json", json);

    auto result = load_manifest(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->entries.size(), 3u);
    EXPECT_EQ(result->entries[0].id, "cube");
    EXPECT_EQ(result->entries[0].path, "assets/cube.obj");
    EXPECT_EQ(result->entries[1].id, "sphere");
    EXPECT_EQ(result->entries[1].path, "assets/sphere.obj");
    EXPECT_EQ(result->entries[2].id, "pyramid");
    EXPECT_EQ(result->entries[2].path, "assets/pyramid.obj");
}

TEST(ManifestTest, EntryMissingIdReturnsError) {
    const std::string json = R"({
        "assets": [
            { "path": "assets/cube.obj" }
        ]
    })";
    const auto path = write_temp_json("snt_test_manifest_no_id.json", json);

    auto result = load_manifest(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("id"), std::string::npos);
}

TEST(ManifestTest, EntryMissingPathReturnsError) {
    const std::string json = R"({
        "assets": [
            { "id": "cube" }
        ]
    })";
    const auto path = write_temp_json("snt_test_manifest_no_path.json", json);

    auto result = load_manifest(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("path"), std::string::npos);
}

TEST(ManifestTest, DuplicateIdsReturnError) {
    const std::string json = R"({
        "assets": [
            { "id": "cube", "path": "assets/cube.obj" },
            { "id": "cube", "path": "assets/other.obj" }
        ]
    })";
    const auto path = write_temp_json("snt_test_manifest_dup.json", json);

    auto result = load_manifest(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("duplicate"), std::string::npos);
}

TEST(ManifestTest, DuplicatePathsAreAllowed) {
    // Two ids pointing to the same file is valid (aliasing). They get
    // different handles but load the same data.
    const std::string json = R"({
        "assets": [
            { "id": "cube",         "path": "assets/cube.obj" },
            { "id": "default_mesh", "path": "assets/cube.obj" }
        ]
    })";
    const auto path = write_temp_json("snt_test_manifest_alias.json", json);

    auto result = load_manifest(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->entries.size(), 2u);
}

// ===========================================================================
// AssetCache::register_preallocated + path_of
// ===========================================================================

TEST(PreallocatedCacheTest, RegisterAssignsHandlesInOrder) {
    AssetCache<ManifestFakeAsset> cache;
    cache.init(
        [](const std::string&) -> Expected<ManifestFakeAsset*> {
            return new ManifestFakeAsset{};
        },
        [](ManifestFakeAsset* p) { delete p; });

    auto h0 = cache.register_preallocated("a.obj");
    auto h1 = cache.register_preallocated("b.obj");
    auto h2 = cache.register_preallocated("c.obj");

    EXPECT_EQ(h0.id, 0u);
    EXPECT_EQ(h1.id, 1u);
    EXPECT_EQ(h2.id, 2u);
}

TEST(PreallocatedCacheTest, RegisterSamePathReturnsSameHandle) {
    AssetCache<ManifestFakeAsset> cache;
    cache.init(
        [](const std::string&) -> Expected<ManifestFakeAsset*> {
            return new ManifestFakeAsset{};
        },
        [](ManifestFakeAsset* p) { delete p; });

    auto h1 = cache.register_preallocated("a.obj");
    auto h2 = cache.register_preallocated("a.obj");
    EXPECT_EQ(h1.id, h2.id);
}

TEST(PreallocatedCacheTest, PathOfReversesRegistration) {
    AssetCache<ManifestFakeAsset> cache;
    cache.init(
        [](const std::string&) -> Expected<ManifestFakeAsset*> {
            return new ManifestFakeAsset{};
        },
        [](ManifestFakeAsset* p) { delete p; });

    auto h = cache.register_preallocated("path/to/mesh.obj");
    EXPECT_EQ(cache.path_of(h), "path/to/mesh.obj");
}

TEST(PreallocatedCacheTest, PathOfReturnsEmptyForUnknownHandle) {
    AssetCache<ManifestFakeAsset> cache;
    cache.init(
        [](const std::string&) -> Expected<ManifestFakeAsset*> {
            return new ManifestFakeAsset{};
        },
        [](ManifestFakeAsset* p) { delete p; });

    AssetHandle<ManifestFakeAsset> unknown{999u};
    EXPECT_TRUE(cache.path_of(unknown).empty());

    AssetHandle<ManifestFakeAsset> invalid;  // default = kInvalidId
    EXPECT_TRUE(cache.path_of(invalid).empty());
}

TEST(PreallocatedCacheTest, LoadPreallocatedFillsSlots) {
    AssetCache<ManifestFakeAsset> cache;
    int load_count = 0;
    cache.init(
        [&](const std::string& path) -> Expected<ManifestFakeAsset*> {
            ++load_count;
            auto* p = new ManifestFakeAsset{};
            p->value = static_cast<int>(path.size());  // FakeAsset has int value
            return p;
        },
        [](ManifestFakeAsset* p) { delete p; });

    // Pre-allocate 3 slots without loading.
    cache.register_preallocated("aa.obj");
    cache.register_preallocated("bbb.obj");
    cache.register_preallocated("cccc.obj");

    // Nothing loaded yet.
    EXPECT_EQ(load_count, 0);

    auto r = cache.load_preallocated();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(load_count, 3);

    // Verify each slot now has a real asset, with the expected value
    // (path length, set by the fake loader).
    auto* a0 = cache.get(AssetHandle<ManifestFakeAsset>{0});
    ASSERT_NE(a0, nullptr);
    EXPECT_EQ(a0->value, 6);  // "aa.obj" -> 6 chars
    auto* a1 = cache.get(AssetHandle<ManifestFakeAsset>{1});
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value, 7);  // "bbb.obj" -> 7 chars
    auto* a2 = cache.get(AssetHandle<ManifestFakeAsset>{2});
    ASSERT_NE(a2, nullptr);
    EXPECT_EQ(a2->value, 8);  // "cccc.obj" -> 8 chars
}

TEST(PreallocatedCacheTest, LoadPreallocatedPropagatesLoaderError) {
    AssetCache<ManifestFakeAsset> cache;
    cache.init(
        [](const std::string& path) -> Expected<ManifestFakeAsset*> {
            if (path == "missing.obj") {
                return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                        "missing.obj"};
            }
            return new ManifestFakeAsset{};
        },
        [](ManifestFakeAsset* p) { delete p; });

    cache.register_preallocated("ok.obj");
    cache.register_preallocated("missing.obj");

    auto r = cache.load_preallocated();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), snt::core::ErrorCode::kFileNotFound);
}

TEST(PreallocatedCacheTest, ManifestRegistrationProducesStableHandles) {
    // The same manifest, registered twice in two separate caches, must
    // produce identical handle assignments. This is the core correctness
    // property that makes scene files portable across runs.
    const std::string json = R"({
        "assets": [
            { "id": "cube",    "path": "assets/cube.obj" },
            { "id": "sphere",  "path": "assets/sphere.obj" }
        ]
    })";
    const auto path = write_temp_json("snt_test_manifest_stable.json", json);
    auto manifest_result = load_manifest(path);
    ASSERT_TRUE(manifest_result.has_value());
    const AssetManifest& manifest = *manifest_result;

    // Cache A.
    AssetCache<ManifestFakeAsset> cache_a;
    cache_a.init(
        [](const std::string&) -> Expected<ManifestFakeAsset*> {
            return new ManifestFakeAsset{};
        },
        [](ManifestFakeAsset* p) { delete p; });
    for (const auto& e : manifest.entries) {
        cache_a.register_preallocated(e.path);
    }

    // Cache B (separate instance).
    AssetCache<ManifestFakeAsset> cache_b;
    cache_b.init(
        [](const std::string&) -> Expected<ManifestFakeAsset*> {
            return new ManifestFakeAsset{};
        },
        [](ManifestFakeAsset* p) { delete p; });
    for (const auto& e : manifest.entries) {
        cache_b.register_preallocated(e.path);
    }

    // Same manifest -> same handles.
    for (size_t i = 0; i < manifest.entries.size(); ++i) {
        AssetHandle<ManifestFakeAsset> ha = cache_a.register_preallocated(manifest.entries[i].path);
        AssetHandle<ManifestFakeAsset> hb = cache_b.register_preallocated(manifest.entries[i].path);
        EXPECT_EQ(ha.id, hb.id) << "handle mismatch at entry " << i;
        EXPECT_EQ(ha.id, i) << "expected handle " << i << " got " << ha.id;
        // path_of must reverse both consistently.
        EXPECT_EQ(cache_a.path_of(ha), manifest.entries[i].path);
        EXPECT_EQ(cache_b.path_of(hb), manifest.entries[i].path);
    }
}
