// gen_default_scene: standalone tool that generates the default binary
// scene file (scenes/default_scene.bin) consumed by Engine::init.
//
// Why a separate tool:
//   - The scene file is "data" — it should be generated once and checked
//     into the repo, not regenerated at every engine startup.
//   - Using a stub mesh (StubMesh) avoids needing a VulkanDevice just to
//     write the scene; save_scene only reads mesh_cache.path_of() to
//     resolve MeshRef handles to paths, so no real mesh data is needed.
//   - Future: this tool is the seed for a scene editor (add flags to
//     list/edit/import scenes).
//
// Usage:
//   gen_default_scene [output_path]
//   Default output_path = "scenes/default_scene.bin" (relative to CWD).
//
// Scene contents (matches the previous hardcoded Engine::init demo):
//   - Camera entity (Guid=1): position [0,0,3], fov 60, near 0.1, far 100
//   - Cube entity  (Guid=2): position [-1.5,0,0], rotation [-25,35,0]
//   - Cube entity2 (Guid=3): position [1.5,0,0],  rotation [-25,-35,0]
//   Both cubes reference mesh path "assets/cube.obj".

#define SNT_LOG_CHANNEL "gen_scene"

#include "assets/asset_cache.h"
#include "assets/asset_handle.h"
#include "core/expected.h"
#include "core/log.h"
#include "ecs/components.h"
#include "ecs/entity_guid.h"
#include "ecs/world.h"
#include "scene/scene.h"

#include <cstdio>
#include <string>

using snt::assets::AssetCache;
using snt::assets::MeshAssetTag;
using snt::core::Expected;
using snt::ecs::Camera;
using snt::ecs::EntityGuid;
using snt::ecs::MeshRef;
using snt::ecs::Transform;
using snt::ecs::World;
using snt::scene::save_scene;

namespace {

// Stub mesh type — only the path matters for scene serialization.
// save_scene calls mesh_cache.path_of(handle) to resolve MeshRef to a
// path string; the actual mesh data is never touched.
struct StubMesh {
    std::string loaded_from;
};

// Wire up a cache with a stub loader. The loader records the path
// so path_of() can return it later. Caller owns the cache; this just
// calls init() on it.
void init_stub_cache(AssetCache<StubMesh, MeshAssetTag>& cache) {
    cache.init(
        [](const std::string& path) -> Expected<StubMesh*> {
            return new StubMesh{path};
        },
        [](StubMesh* m) { delete m; });
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string output_path =
        (argc > 1) ? argv[1] : "game/scenes/default_scene.bin";

    World world;
    AssetCache<StubMesh, MeshAssetTag> cache;
    init_stub_cache(cache);

    // Pre-load the cube mesh path so we get a stable handle to reference.
    // The handle value doesn't matter — save_scene resolves it back to the
    // path string in the scene file.
    auto mesh_result = cache.load("assets/cube.obj");
    if (!mesh_result) {
        SNT_LOG_ERROR("Failed to load stub mesh: %s",
                      mesh_result.error().format().c_str());
        return 1;
    }
    const auto mesh_handle = *mesh_result;

    // --- Camera entity (Guid=1) ---
    // Matches CameraConfig defaults in config/engine.json.
    {
        EntityGuid guid{1};
        entt::entity e = world.create_entity_with_guid(guid);
        auto& t = world.add_component<Transform>(e);
        t.position[0] = 0.0f;
        t.position[1] = 0.0f;
        t.position[2] = 3.0f;
        auto& c = world.add_component<Camera>(e);
        c.fov        = 60.0f;
        c.near_plane = 0.1f;
        c.far_plane  = 100.0f;
        c.aspect     = 16.0f / 9.0f;  // updated at runtime on resize
    }

    // --- Cube entity 1 (Guid=2) ---
    // Left of origin, tilted down-right.
    {
        EntityGuid guid{2};
        entt::entity e = world.create_entity_with_guid(guid);
        auto& t = world.add_component<Transform>(e);
        t.position[0] = -1.5f;
        t.rotation[0] = -25.0f;  // pitch down
        t.rotation[1] = 35.0f;   // yaw right
        world.add_component<MeshRef>(e, MeshRef{mesh_handle});
    }

    // --- Cube entity 2 (Guid=3) ---
    // Right of origin, tilted down-left (mirror of cube 1).
    {
        EntityGuid guid{3};
        entt::entity e = world.create_entity_with_guid(guid);
        auto& t = world.add_component<Transform>(e);
        t.position[0] = 1.5f;
        t.rotation[0] = -25.0f;
        t.rotation[1] = -35.0f;  // mirror yaw
        world.add_component<MeshRef>(e, MeshRef{mesh_handle});
    }

    auto result = save_scene(world, cache, output_path);
    if (!result) {
        SNT_LOG_ERROR("Failed to save scene: %s",
                      result.error().format().c_str());
        return 1;
    }

    SNT_LOG_INFO("Default scene generated: '%s'", output_path.c_str());
    std::printf("Default scene generated: %s\n", output_path.c_str());
    return 0;
}
