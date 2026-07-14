// Unit tests for AssetHandle and AssetManager lifetime behavior.
//
// GPU residency itself is exercised through the uploader boundary tests; these
// checks keep the value-handle and uninitialized-manager contracts explicit.

#include "assets/asset_handle.h"
#include "assets/asset_manager.h"

#include <gtest/gtest.h>

namespace {

struct FakeAsset {};

}  // namespace

TEST(AssetManagerTest, SeparateUninitializedInstancesHaveIndependentLifetimes) {
    snt::assets::AssetManager first;
    {
        snt::assets::AssetManager second;
        second.shutdown();
    }
    first.shutdown();
}

TEST(AssetHandleTest, DefaultIsInvalid) {
    snt::assets::AssetHandle<FakeAsset> handle;
    EXPECT_FALSE(handle.valid());
    EXPECT_EQ(handle.id, snt::assets::AssetHandle<FakeAsset>::kInvalidId);
}

TEST(AssetHandleTest, ExplicitIdIsValid) {
    snt::assets::AssetHandle<FakeAsset> handle{42};
    EXPECT_TRUE(handle.valid());
    EXPECT_EQ(handle.id, 42u);
}

TEST(AssetHandleTest, EqualityIsById) {
    snt::assets::AssetHandle<FakeAsset> first{1};
    snt::assets::AssetHandle<FakeAsset> same{1};
    snt::assets::AssetHandle<FakeAsset> other{2};
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other);
}
