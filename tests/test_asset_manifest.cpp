// Unit tests for the source-independent AssetManifest parser.
//
// Stable MeshHandle/path assignment now belongs to MeshAssetReferenceRegistry
// and AssetManager's uploader-backed runtime path, not a legacy cache.

#include "assets/asset_manifest.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using snt::assets::AssetManifest;
using snt::assets::parse_manifest;

namespace {

snt::core::Expected<AssetManifest> parse_test_manifest(
    std::string_view source_identity,
    const std::string& contents) {
    return parse_manifest(source_identity, contents);
}

}  // namespace

TEST(ManifestTest, ParseErrorReturnsError) {
    const std::string bad_json = "{ this is not valid json ";
    auto result = parse_test_manifest("snt_test_manifest_bad.json", bad_json);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("parse"), std::string::npos);
}

TEST(ManifestTest, EmptyAssetsArrayIsAccepted) {
    const std::string json = R"({ "assets": [] })";
    auto result = parse_test_manifest("snt_test_manifest_empty.json", json);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->entries.empty());
}

TEST(ManifestTest, MissingAssetsArrayReturnsError) {
    const std::string json = R"({ "not_assets": [] })";
    auto result = parse_test_manifest("snt_test_manifest_no_array.json", json);
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
    auto result = parse_test_manifest("snt_test_manifest_ok.json", json);
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
    auto result = parse_test_manifest("snt_test_manifest_no_id.json", json);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("id"), std::string::npos);
}

TEST(ManifestTest, EntryMissingPathReturnsError) {
    const std::string json = R"({
        "assets": [
            { "id": "cube" }
        ]
    })";
    auto result = parse_test_manifest("snt_test_manifest_no_path.json", json);
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
    auto result = parse_test_manifest("snt_test_manifest_dup.json", json);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message().find("duplicate"), std::string::npos);
}

TEST(ManifestTest, DuplicatePathsAreAllowed) {
    // Separate ids may alias the same source request. The uploader performs
    // canonical GPU-residency de-duplication independently from catalog ids.
    const std::string json = R"({
        "assets": [
            { "id": "cube",         "path": "assets/cube.obj" },
            { "id": "default_mesh", "path": "assets/cube.obj" }
        ]
    })";
    auto result = parse_test_manifest("snt_test_manifest_alias.json", json);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->entries.size(), 2u);
}
