// Unit tests for Scene binary serialization.
//
// Phase 4: validates that a World with entities + components can be
// saved to a binary file and loaded back into a fresh World with
// identical state. Uses a source-free mesh reference registry, so the test
// runs without a GPU.

#include "assets/mesh_asset_reference_registry.h"
#include "core/binary_writer.h"
#include "render/render_components.h"
#include "ecs/entity_guid.h"
#include "ecs/world.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using snt::assets::MeshAssetReferenceRegistry;
using snt::core::BinaryWriter;
using snt::render::Camera;
using snt::ecs::EntityGuid;
using snt::render::MeshRef;
using snt::render::Transform;
using snt::ecs::World;
using snt::scene::load_scene;
using snt::scene::save_scene;

namespace {

struct TestMeshReferences {
    MeshAssetReferenceRegistry registry;
};

// Write a binary scene to a temp file + return its path.
std::string temp_scene_path(const char* suffix) {
    return (std::filesystem::temp_directory_path() /
            ("snt_test_scene_" + std::string(suffix) + ".bin"))
        .string();
}

}  // namespace

// ===========================================================================
// Round-trip: save -> load -> compare
// ===========================================================================

TEST(SceneTest, EmptyWorldRoundTrips) {
    World src;
    TestMeshReferences tc;

    const auto path = temp_scene_path("empty");
    auto sr = save_scene(src, tc.registry, path);
    ASSERT_TRUE(sr.has_value()) << sr.error().format();

    World dst;
    auto lr = load_scene(dst, tc.registry, path);
    ASSERT_TRUE(lr.has_value()) << lr.error().format();

    // Empty world saves 0 entities; dst should also have 0 entities
    // with an EntityGuid component.
    size_t dst_guids = 0;
    dst.registry().view<EntityGuid>().each([&](auto, const auto&) { ++dst_guids; });
    EXPECT_EQ(dst_guids, 0u);
}

TEST(SceneTest, SingleEntityWithTransformRoundTrips) {
    World src;
    TestMeshReferences tc;

    // Create one entity with a known Guid + Transform.
    EntityGuid guid{0x1111111111111111ull};
    entt::entity e = src.create_entity_with_guid(guid);
    ASSERT_TRUE(e != entt::null);
    auto& t = src.add_component<Transform>(e);
    t.position[0] = 5.0f;
    t.position[1] = -3.0f;
    t.position[2] = 10.0f;
    t.rotation[0] = 45.0f;
    t.rotation[1] = -90.0f;
    t.rotation[2] = 180.0f;
    t.scale[0] = 2.0f;
    t.scale[1] = 0.5f;
    t.scale[2] = 3.0f;

    const auto path = temp_scene_path("one_entity");
    auto sr = save_scene(src, tc.registry, path);
    ASSERT_TRUE(sr.has_value()) << sr.error().format();

    // Load into a fresh World.
    World dst;
    TestMeshReferences tc2;
    auto lr = load_scene(dst, tc2.registry, path);
    ASSERT_TRUE(lr.has_value()) << lr.error().format();

    // The entity should be findable by its Guid.
    entt::entity loaded = dst.find_entity_by_guid(guid);
    ASSERT_TRUE(loaded != entt::null);
    ASSERT_TRUE(dst.registry().all_of<Transform>(loaded));

    const auto& loaded_t = dst.registry().get<Transform>(loaded);
    EXPECT_FLOAT_EQ(loaded_t.position[0], 5.0f);
    EXPECT_FLOAT_EQ(loaded_t.position[1], -3.0f);
    EXPECT_FLOAT_EQ(loaded_t.position[2], 10.0f);
    EXPECT_FLOAT_EQ(loaded_t.rotation[0], 45.0f);
    EXPECT_FLOAT_EQ(loaded_t.rotation[1], -90.0f);
    EXPECT_FLOAT_EQ(loaded_t.rotation[2], 180.0f);
    EXPECT_FLOAT_EQ(loaded_t.scale[0], 2.0f);
    EXPECT_FLOAT_EQ(loaded_t.scale[1], 0.5f);
    EXPECT_FLOAT_EQ(loaded_t.scale[2], 3.0f);
}

TEST(SceneTest, MultipleEntitiesRoundTrip) {
    World src;
    TestMeshReferences tc;

    // Camera entity.
    EntityGuid cam_guid{0xCCCCCCCCCCCCCCCCull};
    entt::entity cam = src.create_entity_with_guid(cam_guid);
    auto& cam_t = src.add_component<Transform>(cam);
    cam_t.position[2] = 5.0f;
    auto& cam_c = src.add_component<Camera>(cam);
    cam_c.fov = 75.0f;
    cam_c.near_plane = 0.05f;
    cam_c.far_plane = 1000.0f;
    cam_c.aspect = 21.0f / 9.0f;

    // Cube entity.
    EntityGuid cube_guid{0xABCDEF0123456789ull};
    entt::entity cube = src.create_entity_with_guid(cube_guid);
    auto& cube_t = src.add_component<Transform>(cube);
    cube_t.position[0] = -1.5f;
    cube_t.rotation[1] = 35.0f;

    const auto path = temp_scene_path("multi");
    ASSERT_TRUE(save_scene(src, tc.registry, path).has_value());

    World dst;
    TestMeshReferences tc2;
    ASSERT_TRUE(load_scene(dst, tc2.registry, path).has_value());

    // Verify camera entity.
    entt::entity loaded_cam = dst.find_entity_by_guid(cam_guid);
    ASSERT_TRUE(loaded_cam != entt::null);
    ASSERT_TRUE(dst.registry().all_of<Camera>(loaded_cam));
    const auto& loaded_cam_c = dst.registry().get<Camera>(loaded_cam);
    EXPECT_FLOAT_EQ(loaded_cam_c.fov, 75.0f);
    EXPECT_FLOAT_EQ(loaded_cam_c.aspect, 21.0f / 9.0f);

    // Verify cube entity.
    entt::entity loaded_cube = dst.find_entity_by_guid(cube_guid);
    ASSERT_TRUE(loaded_cube != entt::null);
    ASSERT_TRUE(dst.registry().all_of<Transform>(loaded_cube));
    const auto& loaded_cube_t = dst.registry().get<Transform>(loaded_cube);
    EXPECT_FLOAT_EQ(loaded_cube_t.position[0], -1.5f);
    EXPECT_FLOAT_EQ(loaded_cube_t.rotation[1], 35.0f);
}

// ===========================================================================
// MeshRef path resolution
// ===========================================================================

TEST(SceneTest, MeshRefPathRoundTrips) {
    // MeshRef is serialized as a PATH, not a runtime handle. On load, the
    // reference resolver resolves that path to a handle. A fresh resolver may
    // assign a different numeric value, but the path remains the stable
    // identity.
    World src;
    TestMeshReferences tc;

    // Resolve a fake path to obtain a stable scene-facing handle.
    auto load_r = tc.registry.resolve_mesh("fake/cube.obj");
    ASSERT_TRUE(load_r.has_value());
    const auto src_handle = *load_r;

    EntityGuid guid{0x2222222222222222ull};
    entt::entity e = src.create_entity_with_guid(guid);
    src.add_component<MeshRef>(e, MeshRef{src_handle});

    const auto path = temp_scene_path("meshref");
    ASSERT_TRUE(save_scene(src, tc.registry, path).has_value());

    // Load into a fresh world + reference registry. The new registry assigns
    // handles in its own order; scene identity is preserved through the path.
    World dst;
    TestMeshReferences tc2;
    ASSERT_TRUE(load_scene(dst, tc2.registry, path).has_value());

    entt::entity loaded = dst.find_entity_by_guid(guid);
    ASSERT_TRUE(loaded != entt::null);
    ASSERT_TRUE(dst.registry().all_of<MeshRef>(loaded));
    const auto& loaded_ref = dst.registry().get<MeshRef>(loaded);
    EXPECT_EQ(loaded_ref.handle.id, 0u);  // first asset in tc2

    EXPECT_EQ(tc2.registry.mesh_path(loaded_ref.handle), "fake/cube.obj");
}

// ===========================================================================
// Error handling
// ===========================================================================

TEST(SceneErrorTest, LoadMissingFileReturnsError) {
    World dst;
    TestMeshReferences tc;
    auto r = load_scene(dst, tc.registry, "does/not/exist.bin");
    ASSERT_FALSE(r.has_value());
    // Just check we got an error; specific code/message may vary.
}

TEST(SceneErrorTest, LoadTruncatedFileReturnsError) {
    // Write a file with only the magic bytes (no version/entity_count).
    const auto path = temp_scene_path("truncated");
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.write("SNTS", 4);  // magic only, nothing else
    }
    World dst;
    TestMeshReferences tc;
    auto r = load_scene(dst, tc.registry, path);
    ASSERT_FALSE(r.has_value());
}

TEST(SceneErrorTest, LoadBadMagicReturnsError) {
    const auto path = temp_scene_path("badmagic");
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.write("XXXX", 4);  // wrong magic
    }
    World dst;
    TestMeshReferences tc;
    auto r = load_scene(dst, tc.registry, path);
    ASSERT_FALSE(r.has_value());
}

TEST(SceneErrorTest, LoadUnsupportedVersionReturnsError) {
    const auto path = temp_scene_path("badversion");
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.write("SNTS", 4);            // magic
        uint32_t v = 999;                 // unsupported version
        ofs.write(reinterpret_cast<const char*>(&v), 4);
    }
    World dst;
    TestMeshReferences tc;
    auto r = load_scene(dst, tc.registry, path);
    ASSERT_FALSE(r.has_value());
}

TEST(SceneErrorTest, LoadUnknownComponentReturnsError) {
    const auto path = temp_scene_path("unknown_component");
    BinaryWriter writer;
    writer.write_raw(snt::scene::kSceneMagic, 4);
    writer.write_u32(snt::scene::kSceneVersion);
    writer.write_u32(1);
    writer.write_u64(0xAABBCCDDEEFF0011ull);
    writer.write_u32(1);
    writer.write_u32(999);
    writer.write_u32(0);

    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(writer.buffer().data()),
                  static_cast<std::streamsize>(writer.size()));
    }

    World dst;
    TestMeshReferences tc;
    auto r = load_scene(dst, tc.registry, path);
    ASSERT_FALSE(r.has_value());
}

// ===========================================================================
// EntityGuids are preserved across save/load
// ===========================================================================

TEST(SceneGuidTest, EntityGuidsSurviveRoundTrip) {
    // The core serialization correctness property: after save + load,
    // every entity's Guid must be identical to its pre-save value.
    // This is what makes scene files portable across runs.
    World src;
    TestMeshReferences tc;

    std::vector<EntityGuid> src_guids;
    for (int i = 0; i < 10; ++i) {
        EntityGuid g = src.guid_generator().next();
        src_guids.push_back(g);
        entt::entity e = src.create_entity_with_guid(g);
        src.add_component<Transform>(e);
    }

    const auto path = temp_scene_path("guids");
    ASSERT_TRUE(save_scene(src, tc.registry, path).has_value());

    World dst;
    TestMeshReferences tc2;
    ASSERT_TRUE(load_scene(dst, tc2.registry, path).has_value());

    // Every original Guid must resolve to a live entity in dst.
    for (const EntityGuid& g : src_guids) {
        entt::entity e = dst.find_entity_by_guid(g);
        EXPECT_TRUE(e != entt::null) << "Guid " << g.value << " not found after load";
    }
}
