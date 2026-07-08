// Unit tests for the AssetCache template + AssetHandle.
//
// Uses a FakeAsset type so the tests don't need a VulkanDevice (which
// would require a real GPU + driver). The VulkanMesh-specific path is
// exercised end-to-end by running the engine executable.

#include "assets/asset_cache.h"
#include "assets/asset_handle.h"
#include "core/expected.h"

#include <gtest/gtest.h>

#include <string>

namespace {

// Phantom tag + dummy asset type for the cache tests. FakeAsset serves
// as both the cached type T and the phantom Tag for AssetHandle<T>.
struct FakeAsset {
    int value = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// AssetHandle
// ---------------------------------------------------------------------------

TEST(AssetHandleTest, DefaultIsInvalid) {
    snt::assets::AssetHandle<FakeAsset> h;
    EXPECT_FALSE(h.valid());
    EXPECT_EQ(h.id, snt::assets::AssetHandle<FakeAsset>::kInvalidId);
}

TEST(AssetHandleTest, ExplicitIdIsValid) {
    snt::assets::AssetHandle<FakeAsset> h{42};
    EXPECT_TRUE(h.valid());
    EXPECT_EQ(h.id, 42u);
}

TEST(AssetHandleTest, EqualityIsById) {
    snt::assets::AssetHandle<FakeAsset> a{1};
    snt::assets::AssetHandle<FakeAsset> b{1};
    snt::assets::AssetHandle<FakeAsset> c{2};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ---------------------------------------------------------------------------
// AssetCache
// ---------------------------------------------------------------------------

TEST(AssetCacheTest, InitRejectsNullCallbacks) {
    snt::assets::AssetCache<FakeAsset> cache;
    auto r = cache.init(nullptr, nullptr);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), snt::core::ErrorCode::kInvalidArgument);
}

TEST(AssetCacheTest, LoadReturnsHandleAndDeduplicates) {
    snt::assets::AssetCache<FakeAsset> cache;
    cache.init(
        [](const std::string& path) -> snt::core::Expected<FakeAsset*> {
            if (path == "missing") {
                return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                        "missing"};
            }
            return new FakeAsset{static_cast<int>(path.size())};
        },
        [](FakeAsset* p) { delete p; });

    // First load.
    auto h1 = cache.load("hello");
    ASSERT_TRUE(h1.has_value());
    ASSERT_TRUE(h1->valid());
    auto* p1 = cache.get(*h1);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(p1->value, 5);  // strlen("hello") == 5

    // Same path -> same handle (dedup).
    auto h2 = cache.load("hello");
    ASSERT_TRUE(h2.has_value());
    EXPECT_EQ(h2->id, h1->id);

    // Different path -> different handle.
    auto h3 = cache.load("world!");
    ASSERT_TRUE(h3.has_value());
    EXPECT_NE(h3->id, h1->id);
    EXPECT_EQ(cache.get(*h3)->value, 6);

    EXPECT_EQ(cache.size(), 2u);
}

TEST(AssetCacheTest, LoadPropagatesErrorFromLoader) {
    snt::assets::AssetCache<FakeAsset> cache;
    cache.init(
        [](const std::string&) -> snt::core::Expected<FakeAsset*> {
            return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                    "missing"};
        },
        [](FakeAsset* p) { delete p; });

    auto r = cache.load("anything");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), snt::core::ErrorCode::kFileNotFound);
}

TEST(AssetCacheTest, GetInvalidHandleReturnsNull) {
    snt::assets::AssetCache<FakeAsset> cache;
    cache.init(
        [](const std::string& path) -> snt::core::Expected<FakeAsset*> {
            return new FakeAsset{static_cast<int>(path.size())};
        },
        [](FakeAsset* p) { delete p; });

    EXPECT_EQ(cache.get(snt::assets::AssetHandle<FakeAsset>{}), nullptr);
    EXPECT_EQ(cache.get(snt::assets::AssetHandle<FakeAsset>{999u}), nullptr);
}

TEST(AssetCacheTest, DestroyReleasesAllAssets) {
    int destroy_count = 0;
    snt::assets::AssetCache<FakeAsset> cache;
    cache.init(
        [](const std::string& path) -> snt::core::Expected<FakeAsset*> {
            return new FakeAsset{static_cast<int>(path.size())};
        },
        [&destroy_count](FakeAsset* p) { ++destroy_count; delete p; });

    (void)cache.load("a");
    (void)cache.load("b");
    (void)cache.load("c");
    ASSERT_EQ(cache.size(), 3u);
    EXPECT_EQ(destroy_count, 0);

    cache.destroy();
    EXPECT_EQ(destroy_count, 3);
    EXPECT_EQ(cache.size(), 0u);

    // Destroy is idempotent.
    cache.destroy();
    EXPECT_EQ(destroy_count, 3);
}

TEST(AssetCacheTest, ReloadStubReturnsNotImplemented) {
    snt::assets::AssetCache<FakeAsset> cache;
    cache.init(
        [](const std::string&) -> snt::core::Expected<FakeAsset*> {
            return new FakeAsset{};
        },
        [](FakeAsset* p) { delete p; });

    auto h = cache.load("x");
    ASSERT_TRUE(h.has_value());
    auto r = cache.reload(*h);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), snt::core::ErrorCode::kNotImplemented);
}
