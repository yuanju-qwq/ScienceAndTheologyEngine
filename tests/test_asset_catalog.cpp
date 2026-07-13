// Contract tests for the source-backed immutable AssetCatalog.

#include "assets/asset_catalog.h"
#include "core/error.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

class StubAssetSource final : public snt::assets::IAssetSource {
public:
    StubAssetSource(std::string canonical_path, std::string text) {
        data_.canonical_path = std::move(canonical_path);
        data_.bytes.assign(text.begin(), text.end());
    }

    explicit StubAssetSource(snt::core::Error error)
        : error_(std::move(error)) {}

    [[nodiscard]] snt::core::Expected<snt::assets::AssetSourceData> read(
        snt::assets::AssetSourceRequest request) override {
        last_request_ = std::move(request.requested_path);
        if (error_) {
            return *error_;
        }
        return data_;
    }

    [[nodiscard]] const std::string& last_request() const noexcept {
        return last_request_;
    }

private:
    snt::assets::AssetSourceData data_;
    std::optional<snt::core::Error> error_;
    std::string last_request_;
};

TEST(AssetCatalogTest, LoadsManifestFromSourceAndResolvesStableIds) {
    StubAssetSource source(
        "game://config/default_manifest.json",
        R"({"assets":[{"id":"cube","path":"assets/dev/cube.obj"},
                      {"id":"default_cube","path":"assets/dev/cube.obj"}]})");

    auto catalog = snt::assets::AssetCatalog::load(
        source, {"config/default_manifest.json"});
    ASSERT_TRUE(catalog.has_value()) << catalog.error().format();
    EXPECT_EQ(source.last_request(), "config/default_manifest.json");
    EXPECT_EQ(catalog->manifest_identity(), "game://config/default_manifest.json");
    EXPECT_EQ(catalog->size(), 2u);

    auto cube = catalog->resolve("cube");
    ASSERT_TRUE(cube.has_value()) << cube.error().format();
    EXPECT_EQ(cube->requested_path, "assets/dev/cube.obj");

    auto alias = catalog->resolve("default_cube");
    ASSERT_TRUE(alias.has_value()) << alias.error().format();
    EXPECT_EQ(alias->requested_path, cube->requested_path);

    auto missing = catalog->resolve("not_declared");
    ASSERT_FALSE(missing.has_value()) << missing.error().format();
    EXPECT_EQ(missing.error().code(), snt::core::ErrorCode::kAssetNotFound);
}

TEST(AssetCatalogTest, MissingManifestProducesEmptyCatalog) {
    StubAssetSource source(snt::core::Error{
        snt::core::ErrorCode::kFileNotFound,
        "manifest not present"});

    auto catalog = snt::assets::AssetCatalog::load(source, {"config/missing.json"});
    ASSERT_TRUE(catalog.has_value()) << catalog.error().format();
    EXPECT_EQ(catalog->manifest_identity(), "config/missing.json");
    EXPECT_EQ(catalog->size(), 0u);
}

TEST(AssetCatalogTest, InvalidManifestPropagatesStructuredError) {
    StubAssetSource source("game://config/bad_manifest.json", "{not json}");

    auto catalog = snt::assets::AssetCatalog::load(source, {"config/bad_manifest.json"});
    ASSERT_FALSE(catalog.has_value());
    EXPECT_EQ(catalog.error().code(), snt::core::ErrorCode::kInvalidArgument);
    EXPECT_NE(catalog.error().context().find("AssetCatalog::load"), std::string::npos);
}

}  // namespace
