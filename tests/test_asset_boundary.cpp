// Contract tests for the staged asset source / GPU residency boundary.
//
// These fakes deliberately contain no filesystem, Vulkan, World, or runtime
// singleton. They verify that the public interfaces preserve owned data and
// value-based handoff before AssetManager is migrated to use them.

#include "assets/asset_source.h"
#include "assets/gpu_asset_uploader.h"
#include "core/error.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using snt::assets::AssetSourceData;
using snt::assets::AssetSourceRequest;
using snt::assets::GpuAssetKind;
using snt::assets::GpuAssetResidencyToken;
using snt::assets::GpuAssetUploadRequest;
using snt::assets::IAssetSource;
using snt::assets::IGpuAssetUploader;

class InMemoryAssetSource final : public IAssetSource {
public:
    [[nodiscard]] snt::core::Expected<AssetSourceData> read(
        AssetSourceRequest request) override {
        if (request.requested_path != "catalog://starter-cube") {
            return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                    "asset not present in test source"};
        }

        AssetSourceData result;
        result.canonical_path = "game://assets/dev/cube.obj";
        result.bytes = bytes_;
        return result;
    }

    [[nodiscard]] std::uint8_t first_source_byte() const {
        return bytes_.front();
    }

private:
    std::vector<std::uint8_t> bytes_ = {0x01, 0x02, 0x03};
};

class RecordingGpuAssetUploader final : public IGpuAssetUploader {
public:
    [[nodiscard]] snt::core::Expected<GpuAssetResidencyToken> upload(
        GpuAssetUploadRequest request) override {
        uploads_.push_back(std::move(request));
        return GpuAssetResidencyToken{next_token_++};
    }

    [[nodiscard]] snt::core::Expected<void> release(
        GpuAssetResidencyToken token) override {
        if (!token.valid()) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "cannot release an invalid token"};
        }
        released_.push_back(token);
        return {};
    }

    [[nodiscard]] snt::core::Expected<std::size_t> evict_unused() override {
        const std::size_t count = released_.size();
        released_.clear();
        return count;
    }

    [[nodiscard]] const std::vector<GpuAssetUploadRequest>& uploads() const {
        return uploads_;
    }

private:
    std::uint64_t next_token_ = 1;
    std::vector<GpuAssetUploadRequest> uploads_;
    std::vector<GpuAssetResidencyToken> released_;
};

using UploadSignature = snt::core::Expected<GpuAssetResidencyToken> (
    IGpuAssetUploader::*)(GpuAssetUploadRequest);
static_assert(std::is_same_v<decltype(&IGpuAssetUploader::upload), UploadSignature>);

}  // namespace

TEST(AssetBoundaryTest, SourceReturnsOwnedCanonicalData) {
    InMemoryAssetSource source;

    AssetSourceRequest request;
    request.requested_path = "catalog://starter-cube";
    auto result = source.read(std::move(request));

    ASSERT_TRUE(result.has_value()) << result.error().format();
    EXPECT_EQ(result->canonical_path, "game://assets/dev/cube.obj");
    ASSERT_EQ(result->bytes.size(), 3u);
    result->bytes[0] = 0xff;
    EXPECT_EQ(source.first_source_byte(), 0x01);
}

TEST(AssetBoundaryTest, UploaderTakesValueRequestAndReclaimsReleasedTokens) {
    InMemoryAssetSource source;
    AssetSourceRequest source_request;
    source_request.requested_path = "catalog://starter-cube";
    auto source_result = source.read(std::move(source_request));
    ASSERT_TRUE(source_result.has_value()) << source_result.error().format();

    GpuAssetUploadRequest request;
    request.kind = GpuAssetKind::kMesh;
    request.source = std::move(*source_result);

    RecordingGpuAssetUploader uploader;
    auto token = uploader.upload(std::move(request));
    ASSERT_TRUE(token.has_value()) << token.error().format();
    EXPECT_TRUE(token->valid());
    ASSERT_EQ(uploader.uploads().size(), 1u);
    EXPECT_EQ(uploader.uploads().front().kind, GpuAssetKind::kMesh);
    EXPECT_EQ(uploader.uploads().front().source.canonical_path,
              "game://assets/dev/cube.obj");

    auto release = uploader.release(*token);
    ASSERT_TRUE(release.has_value()) << release.error().format();
    auto evicted = uploader.evict_unused();
    ASSERT_TRUE(evicted.has_value()) << evicted.error().format();
    EXPECT_EQ(*evicted, 1u);
}
