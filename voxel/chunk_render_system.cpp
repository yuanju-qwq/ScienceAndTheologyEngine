// ChunkRenderSystem implementation.

#define SNT_LOG_CHANNEL "voxel"
#include "core/log.h"

#include "voxel/chunk_render_system.h"

#include "data/defs/chunk_data.h"      // ChunkData
#include "data/defs/terrain_data.h"    // TerrainData
#include "data/world/chunk_registry.h" // ChunkRegistry
#include "ecs/components.h"            // ChunkRenderRef
#include "ecs/world.h"                 // World
#include "voxel/chunk_renderer.h"
#include "voxel/greedy_mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>

namespace snt::voxel {

namespace {

// Build a flat materials byte-array from a chunk's TerrainData. Each cell's
// `material` field is extracted in terrain-index order (y * size_z + z) *
// size_x + x, matching greedy_mesh's indexing.
std::vector<uint8_t> extract_materials(const snt::data::TerrainData& terrain) {
    const size_t total = static_cast<size_t>(terrain.size_x) *
                         static_cast<size_t>(terrain.size_y) *
                         static_cast<size_t>(terrain.size_z);
    std::vector<uint8_t> materials;
    materials.reserve(total);
    for (size_t i = 0; i < total && i < terrain.cells.size(); ++i) {
        materials.push_back(terrain.cells[i].material);
    }
    return materials;
}

// Build a chunk's world model matrix as a row of 16 floats (column-major,
// matching UniformBufferObject.model layout). Chunks are axis-aligned, so
// the model matrix is a pure translation by (cx, cy, cz) * kChunkSize.
void build_chunk_model(int32_t cx, int32_t cy, int32_t cz, float out[16]) {
    constexpr int32_t kChunkSize = snt::data::ChunkData::kChunkSize;  // 32
    glm::mat4 m = glm::translate(glm::mat4(1.0f),
                                 glm::vec3(static_cast<float>(cx * kChunkSize),
                                           static_cast<float>(cy * kChunkSize),
                                           static_cast<float>(cz * kChunkSize)));
    std::memcpy(out, glm::value_ptr(m), sizeof(float) * 16);
}

}  // namespace

// ---------------------------------------------------------------------------
// Phase 1: remesh dirty chunks
// ---------------------------------------------------------------------------

void ChunkRenderSystem::update(snt::ecs::World& world, float /*dt*/) {
    if (!renderer_ || !registry_) return;

    auto& reg = world.registry();
    auto view = reg.view<snt::ecs::ChunkRenderRef>();
    for (auto e : view) {
        auto& ref = view.get<snt::ecs::ChunkRenderRef>(e);
        if (!ref.dirty) continue;
        remesh_chunk(world, e, ref);
    }
}

void ChunkRenderSystem::remesh_chunk(snt::ecs::World& /*world*/,
                                     entt::entity /*e*/,
                                     snt::ecs::ChunkRenderRef& ref) {
    const std::string dimension = "overworld";
    const snt::data::ChunkData* chunk =
        registry_->get_chunk(dimension, ref.chunk_x, ref.chunk_y, ref.chunk_z);
    if (!chunk) {
        SNT_LOG_WARN("ChunkRenderSystem: chunk (%d,%d,%d) missing from registry",
                     ref.chunk_x, ref.chunk_y, ref.chunk_z);
        ref.dirty = false;
        return;
    }

    // Extract materials from the chunk's terrain volume.
    const auto materials = extract_materials(chunk->terrain);
    const int sx = chunk->terrain.size_x;
    const int sy = chunk->terrain.size_y;
    const int sz = chunk->terrain.size_z;

    // No neighbors loaded yet: empty NeighborMaterials. Boundary faces
    // will emit (correct for a lone chunk).
    NeighborMaterials neighbors;

    VoxelMeshData mesh = build_greedy_mesh(
        materials, sx, sy, sz,
        air_material_, ladder_material_,
        transparent_mask_, neighbors);

    // Release any previously uploaded mesh before uploading the new one.
    if (ref.mesh_handle_id != snt::ecs::ChunkRenderRef{}.mesh_handle_id) {
        renderer_->unload_mesh(ChunkMeshHandle{ref.mesh_handle_id});
        ref.mesh_handle_id = snt::ecs::ChunkRenderRef{}.mesh_handle_id;
    }

    auto upload_r = renderer_->upload_mesh(mesh);
    if (!upload_r) {
        SNT_LOG_ERROR("ChunkRenderSystem: upload_mesh failed for chunk (%d,%d,%d): %s",
                      ref.chunk_x, ref.chunk_y, ref.chunk_z,
                      upload_r.error().format().c_str());
        ref.dirty = false;
        return;
    }
    ref.mesh_handle_id = upload_r->id;
    ref.dirty = false;

    SNT_LOG_DEBUG("ChunkRenderSystem: remeshed chunk (%d,%d,%d) -> %zu verts, %zu idx",
                  ref.chunk_x, ref.chunk_y, ref.chunk_z,
                  mesh.vertices.size(), mesh.indices.size());
}

// ---------------------------------------------------------------------------
// Phase 2: record draws
// ---------------------------------------------------------------------------

void ChunkRenderSystem::render(VkCommandBuffer cmd, uint32_t frame_idx,
                               const float view[16], const float proj[16],
                               snt::ecs::World& world) {
    if (!renderer_) return;

    draw_scratch_.clear();
    const uint32_t max_chunks = renderer_->max_chunks();

    auto& reg = world.registry();
    auto view_group = reg.view<snt::ecs::ChunkRenderRef>();
    for (auto e : view_group) {
        if (draw_scratch_.size() >= max_chunks) {
            SNT_LOG_ERROR("ChunkRenderSystem: too many chunks to draw (>=%u), truncating",
                          max_chunks);
            break;
        }
        const auto& ref = view_group.get<snt::ecs::ChunkRenderRef>(e);
        // Skip chunks with no uploaded mesh (empty chunks or not yet remeshed).
        if (ref.mesh_handle_id == snt::ecs::ChunkRenderRef{}.mesh_handle_id) {
            continue;
        }

        ChunkDrawCall dc;
        dc.mesh_handle = ChunkMeshHandle{ref.mesh_handle_id};
        build_chunk_model(ref.chunk_x, ref.chunk_y, ref.chunk_z, dc.model);
        draw_scratch_.push_back(dc);
    }

    if (draw_scratch_.empty()) return;

    renderer_->render(cmd, frame_idx, view, proj,
                      draw_scratch_.data(),
                      static_cast<uint32_t>(draw_scratch_.size()));
}

}  // namespace snt::voxel
