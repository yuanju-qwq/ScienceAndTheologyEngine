// gen_default_scene: standalone tool that generates the default binary
// scene file (scenes/default_scene.bin) consumed by the game session.
//
// Why a separate tool:
//   - The scene file is "data" — it should be generated once and checked
//     into the repo, not regenerated at every engine startup.
//   - MeshAssetReferenceRegistry avoids needing a VulkanDevice: scene writing
//     needs only stable MeshHandle/path identity, not real mesh data or GPU
//     residency.
//   - Future: this tool is the seed for a scene editor (add flags to
//     list/edit/import scenes).
//
// Usage:
//   gen_default_scene [output_path]
//   Default output_path = "scenes/default_scene.bin" (relative to CWD).
//
// Scene contents match the ScienceAndTheology session bootstrap.
//   - Camera entity (Guid=1): position [0,0,3], fov 60, near 0.1, far 100
//   - Cube entity  (Guid=2): position [-1.5,0,0], rotation [-25,35,0]
//   - Cube entity2 (Guid=3): position [1.5,0,0],  rotation [-25,-35,0]
//   Both cubes reference mesh path "assets/dev/cube.obj".

#define SNT_LOG_CHANNEL "gen_scene"

#include "assets/mesh_asset_reference_registry.h"
#include "core/log.h"
#include "render/render_components.h"
#include "ecs/entity_guid.h"
#include "ecs/world.h"
#include "scene/scene.h"

#include <cstdio>
#include <string>

using snt::assets::MeshAssetReferenceRegistry;
using snt::render::Camera;
using snt::ecs::EntityGuid;
using snt::render::MeshRef;
using snt::render::Transform;
using snt::ecs::World;
using snt::scene::save_scene;

int main(int argc, char* argv[]) {
    const std::string output_path =
        (argc > 1) ? argv[1] : "default_scene.bin";

    World world;
    MeshAssetReferenceRegistry mesh_references;

    // Register the cube path so the scene receives a stable mesh handle.
    auto mesh_result = mesh_references.resolve_mesh("assets/dev/cube.obj");
    if (!mesh_result) {
        SNT_LOG_ERROR("Failed to register scene mesh reference: %s",
                      mesh_result.error().format().c_str());
        return 1;
    }
    const auto mesh_handle = *mesh_result;

    // --- Camera entity (Guid=1) ---
    // Matches CameraConfig defaults in the game-owned engine configuration.
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

    auto result = save_scene(world, mesh_references, output_path);
    if (!result) {
        SNT_LOG_ERROR("Failed to save scene: %s",
                      result.error().format().c_str());
        return 1;
    }

    SNT_LOG_INFO("Default scene generated: '%s'", output_path.c_str());
    std::printf("Default scene generated: %s\n", output_path.c_str());
    return 0;
}
