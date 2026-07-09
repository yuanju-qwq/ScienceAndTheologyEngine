// ChunkRenderSystem — ECS system that drives chunk meshing + draw recording.
//
// Two-phase design (keeps the single frame loop in RenderSystem):
//   1. update(world, dt)  — runs during world.update(dt). For every dirty
//      ChunkRenderRef entity: fetch ChunkData from ChunkRegistry, run the
//      greedy mesher, upload the mesh via ChunkRenderer, store the handle
//      in the component, clear dirty. NO draws, NO frame acquisition.
//   2. render(cmd, frame_idx, view, proj, world) — called by RenderSystem
//      inside its forward pass callback (command buffer is recording).
//      Builds a ChunkDrawCall list from all ChunkRenderRef entities and
//      hands it to ChunkRenderer::render.
//
// Layering: sits in voxel/, depends on ecs + data (ChunkRegistry +
// ChunkData) + voxel_mesh (greedy_mesher) + chunk_renderer. No Vulkan
// types leak into the header except VkCommandBuffer (opaque handle).

#pragma once

#include "core/expected.h"        // Expected<void>
#include "ecs/system.h"           // System base
#include "ecs/entt_config.h"      // entt::entity
#include "voxel/chunk_renderer.h" // ChunkRenderer, ChunkDrawCall, ChunkMeshHandle
#include "voxel/voxel_vertex.h"   // VoxelMeshData (used internally)

#include <vulkan/vulkan.h>        // VkCommandBuffer

#include <cstdint>
#include <vector>

namespace snt::data  { class ChunkRegistry; }
namespace snt::ecs   { struct ChunkRenderRef; }

namespace snt::voxel {

class ChunkRenderSystem : public snt::ecs::System {
public:
    ChunkRenderSystem() = default;
    ~ChunkRenderSystem() override = default;

    // Wire up dependencies. All pointers are borrowed and must outlive the
    // system. ChunkRenderer is required for both update + render; the
    // ChunkRegistry is required for update (to fetch ChunkData).
    void set_chunk_renderer(ChunkRenderer* r) { renderer_ = r; }
    void set_chunk_registry(snt::data::ChunkRegistry* r) { registry_ = r; }

    // Material parameters used by the greedy mesher. Defaults match the
    // TerrainGenerator's conventions (air=0, ladder=255, no transparent
    // materials, no collidable materials). Override for game-specific
    // material tables.
    void set_air_material(int32_t m)       { air_material_ = m; }
    void set_ladder_material(int32_t m)    { ladder_material_ = m; }
    void set_transparent_mask(const std::vector<uint8_t>& m) { transparent_mask_ = m; }

    // ECS update: remesh dirty chunks (phase 1).
    void update(snt::ecs::World& world, float dt) override;

    // Render phase (phase 2). Called by RenderSystem inside the forward
    // pass. Builds ChunkDrawCalls for every ChunkRenderRef entity with a
    // valid mesh handle and forwards them to ChunkRenderer::render.
    void render(VkCommandBuffer cmd, uint32_t frame_idx,
                const float view[16], const float proj[16],
                snt::ecs::World& world);

private:
    // Remesh one chunk: fetch materials from ChunkRegistry, run greedy
    // mesh, upload via ChunkRenderer, update the component.
    void remesh_chunk(snt::ecs::World& world, entt::entity e,
                      snt::ecs::ChunkRenderRef& ref);

    ChunkRenderer*              renderer_      = nullptr;
    snt::data::ChunkRegistry*   registry_      = nullptr;

    // Mesher parameters.
    int32_t             air_material_    = 0;
    int32_t             ladder_material_ = 255;
    std::vector<uint8_t> transparent_mask_;

    // Scratch buffer for the per-frame draw list (reused across frames to
    // avoid per-frame allocation).
    std::vector<ChunkDrawCall> draw_scratch_;
};

}  // namespace snt::voxel
