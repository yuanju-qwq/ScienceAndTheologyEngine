// Generic presentation component regression coverage.

#include "render/render_components.h"

#include <gtest/gtest.h>

TEST(RenderMeshLodTest, KeepsPrimaryMeshWithinFullDetailDistance) {
    const snt::render::MeshRef primary{{.id = 3}};
    const snt::render::MeshLod lod{
        .simplified_handle = {.id = 7},
        .simplified_detail_distance = 12.0f,
        .cull_distance = 80.0f,
    };

    const auto selection = snt::render::select_mesh_lod(primary, &lod, 144.0f);

    EXPECT_EQ(selection.level, snt::render::MeshLodLevel::kFull);
    EXPECT_EQ(selection.handle.id, 3u);
}

TEST(RenderMeshLodTest, UsesSimplifiedMeshForDistantVisibleEntity) {
    const snt::render::MeshRef primary{{.id = 3}};
    const snt::render::MeshLod lod{
        .simplified_handle = {.id = 7},
        .simplified_detail_distance = 12.0f,
        .cull_distance = 80.0f,
    };

    const auto selection = snt::render::select_mesh_lod(primary, &lod, 169.0f);

    EXPECT_EQ(selection.level, snt::render::MeshLodLevel::kSimplified);
    EXPECT_EQ(selection.handle.id, 7u);
}

TEST(RenderMeshLodTest, CullsBeyondConfiguredDistanceAndFallsBackWithoutSimplifiedMesh) {
    const snt::render::MeshRef primary{{.id = 3}};
    const snt::render::MeshLod lod{
        .simplified_detail_distance = 12.0f,
        .cull_distance = 80.0f,
    };

    const auto fallback = snt::render::select_mesh_lod(primary, &lod, 169.0f);
    EXPECT_EQ(fallback.level, snt::render::MeshLodLevel::kFull);
    EXPECT_EQ(fallback.handle.id, 3u);

    const auto culled = snt::render::select_mesh_lod(primary, &lod, 6401.0f);
    EXPECT_EQ(culled.level, snt::render::MeshLodLevel::kCulled);
    EXPECT_FALSE(culled.handle.valid());
}
