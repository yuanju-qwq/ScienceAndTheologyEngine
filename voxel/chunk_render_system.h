// ChunkRenderSystem — drives chunk meshing + draw recording for STATIC
// terrain (non-entity blocks). Entity blocks (machines, trees, etc.) are
// handled separately and do not flow through this system.
//
// Design (pure-data, no ECS entities per chunk):
//   Chunks are bulk objects (thousands at scale). Modeling each as an ECS
//   entity with a ChunkRenderRef component adds per-chunk overhead (EntityGuid
//   allocation, reverse map, component pool, view iteration) that is
//   avoidable for static terrain. Instead this system owns its own state:
//     - dirty_chunks_   : set of ChunkKeys awaiting remesh
//     - uploaded_meshes_: ChunkKey -> uploaded ChunkMeshHandle
//   The system reads chunk data directly from ChunkRegistry.
//
// Two-phase (keeps the single frame loop in RenderSystem):
//   1. update(world, dt)  — remesh every dirty chunk: fetch ChunkData from
//      ChunkRegistry, run greedy mesher, upload via ChunkRenderer, store
//      the handle in uploaded_meshes_, clear dirty. NO draws, NO frame
//      acquisition.
//   2. render(cmd, frame_idx, view, proj) — called by RenderSystem inside
//      its forward pass callback. Builds a ChunkDrawCall list from
//      uploaded_meshes_ and forwards to ChunkRenderer::render.
//
// Public API for external dirty-marking (e.g. after terrain edit):
//   mark_dirty(ChunkKey)  — schedule a chunk for remesh on next update()
//   untrack(ChunkKey)     — unload mesh + drop from tracking (chunk removed)
//
// Layering: sits in voxel/, depends on data (ChunkRegistry + ChunkData) +
// voxel_mesh (greedy_mesher) + chunk_renderer. Still subclasses ecs::System
// so it plugs into World::update() scheduling, but holds no per-chunk
// entities. No Vulkan types leak into the header except VkCommandBuffer.

#pragma once

#include "core/expected.h"        // Expected<void>
#include "core/job_system.h"      // Future for async remesh jobs
#include "data/defs/chunk_data.h" // ChunkKey
#include "ecs/system.h"           // System base
#include "voxel/chunk_renderer.h" // ChunkRenderer, ChunkDrawCall, ChunkMeshHandle
#include "voxel/voxel_vertex.h"   // VoxelMeshData (used internally)

#include <vulkan/vulkan.h>        // VkCommandBuffer

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace snt::data { class ChunkRegistry; }

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
    void set_remesh_jobs_per_frame(uint32_t n) { remesh_jobs_per_frame_ = n > 0 ? n : 1; }
    void set_uploads_per_frame(uint32_t n) { uploads_per_frame_ = n > 0 ? n : 1; }

    // --- External dirty-marking API ---
    // Schedule a chunk for remesh on the next update(). Safe to call for
    // chunks not yet tracked (they will be added). Idempotent.
    void mark_dirty(const snt::data::ChunkKey& key) { dirty_chunks_.insert(key); }

    // Untrack a chunk: unload its mesh (if uploaded) and remove from all
    // internal maps. Call when a chunk is unloaded from ChunkRegistry.
    void untrack(const snt::data::ChunkKey& key);

    // ECS update: remesh dirty chunks (phase 1).
    void update(snt::ecs::World& world, float dt) override;

    // Render phase (phase 2). Called by RenderSystem inside the forward
    // pass. Builds ChunkDrawCalls for every uploaded mesh and forwards
    // them to ChunkRenderer::render.
    void render(VkCommandBuffer cmd, uint32_t frame_idx,
                const float view[16], const float proj[16]);

private:
    struct RemeshResult {
        snt::data::ChunkKey key;
        VoxelMeshData mesh;
        bool ok = false;
    };

    struct PendingRemesh {
        snt::data::ChunkKey key;
        snt::core::Future<RemeshResult> future;
    };

    // Main-thread phase: copy chunk material data and schedule worker jobs.
    void schedule_dirty_remeshes();

    // Main-thread phase: poll completed worker jobs and upload results.
    void upload_ready_remeshes();
    void upload_remesh_result(RemeshResult&& result);

    ChunkRenderer*              renderer_      = nullptr;
    snt::data::ChunkRegistry*   registry_      = nullptr;

    // Mesher parameters.
    int32_t             air_material_    = 0;
    int32_t             ladder_material_ = 255;
    std::vector<uint8_t> transparent_mask_;

    // Pure-data chunk tracking (no ECS entities).
    std::unordered_set<snt::data::ChunkKey>   dirty_chunks_;
    std::unordered_set<snt::data::ChunkKey>   pending_chunks_;
    std::vector<PendingRemesh>                pending_remeshes_;
    std::unordered_map<snt::data::ChunkKey, ChunkMeshHandle> uploaded_meshes_;

    uint32_t remesh_jobs_per_frame_ = 4;
    uint32_t uploads_per_frame_ = 2;

    // Scratch buffer for the per-frame draw list (reused across frames to
    // avoid per-frame allocation).
    std::vector<ChunkDrawCall> draw_scratch_;
};

}  // namespace snt::voxel
