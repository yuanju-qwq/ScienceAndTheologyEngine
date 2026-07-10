// ChunkRenderSystem implementation.

#define SNT_LOG_CHANNEL "voxel"
#include "core/log.h"

#include "voxel/chunk_render_system.h"

#include "data/defs/chunk_data.h"      // ChunkData
#include "data/defs/terrain_data.h"    // TerrainData
#include "data/world/chunk_registry.h" // ChunkRegistry
#include "voxel/chunk_renderer.h"
#include "voxel/greedy_mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <utility>

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
// External dirty-marking API
// ---------------------------------------------------------------------------

void ChunkRenderSystem::untrack(const snt::data::ChunkKey& key) {
    auto it = uploaded_meshes_.find(key);
    if (it != uploaded_meshes_.end()) {
        if (renderer_) {
            renderer_->unload_mesh(it->second);
        }
        uploaded_meshes_.erase(it);
    }
    dirty_chunks_.erase(key);
}

// ---------------------------------------------------------------------------
// Phase 1: remesh dirty chunks
// ---------------------------------------------------------------------------

void ChunkRenderSystem::update(snt::ecs::World& /*world*/, float /*dt*/) {
    if (!renderer_ || !registry_ || dirty_chunks_.empty()) return;

    // Snapshot the dirty set so mark_dirty() can be safely called from
    // within remesh_chunk (e.g. neighbor invalidation) without invalidating
    // the iterator.
    std::vector<snt::data::ChunkKey> snapshot;
    snapshot.reserve(dirty_chunks_.size());
    for (const auto& k : dirty_chunks_) snapshot.push_back(k);

    for (const auto& key : snapshot) {
        remesh_chunk(key);
        dirty_chunks_.erase(key);
    }
}

void ChunkRenderSystem::remesh_chunk(const snt::data::ChunkKey& key) {
    const snt::data::ChunkData* chunk =
        registry_->get_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (!chunk) {
        SNT_LOG_WARN("ChunkRenderSystem: chunk (%d,%d,%d) '%s' missing from registry",
                     key.chunk_x, key.chunk_y, key.chunk_z, key.dimension_id.c_str());
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
    auto it = uploaded_meshes_.find(key);
    if (it != uploaded_meshes_.end()) {
        renderer_->unload_mesh(it->second);
        uploaded_meshes_.erase(it);
    }

    // Skip empty meshes (fully air chunks) — no draw needed.
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        SNT_LOG_DEBUG("ChunkRenderSystem: chunk (%d,%d,%d) empty, skip upload",
                      key.chunk_x, key.chunk_y, key.chunk_z);
        return;
    }

    auto upload_r = renderer_->upload_mesh(mesh);
    if (!upload_r) {
        SNT_LOG_ERROR("ChunkRenderSystem: upload_mesh failed for chunk (%d,%d,%d): %s",
                      key.chunk_x, key.chunk_y, key.chunk_z,
                      upload_r.error().format().c_str());
        return;
    }
    uploaded_meshes_[key] = *upload_r;

    SNT_LOG_DEBUG("ChunkRenderSystem: remeshed chunk (%d,%d,%d) -> %zu verts, %zu idx",
                  key.chunk_x, key.chunk_y, key.chunk_z,
                  mesh.vertices.size(), mesh.indices.size());
}

// ---------------------------------------------------------------------------
// Phase 2: record draws
// ---------------------------------------------------------------------------

void ChunkRenderSystem::render(VkCommandBuffer cmd, uint32_t frame_idx,
                               const float view[16], const float proj[16]) {
    if (!renderer_ || uploaded_meshes_.empty()) return;

    draw_scratch_.clear();
    const uint32_t max_chunks = renderer_->max_chunks();

    for (const auto& [key, handle] : uploaded_meshes_) {
        if (draw_scratch_.size() >= max_chunks) {
            SNT_LOG_ERROR("ChunkRenderSystem: too many chunks to draw (>=%u), truncating",
                          max_chunks);
            break;
        }
        ChunkDrawCall dc;
        dc.mesh_handle = handle;
        build_chunk_model(key.chunk_x, key.chunk_y, key.chunk_z, dc.model);
        draw_scratch_.push_back(dc);
    }

    if (draw_scratch_.empty()) return;

    renderer_->render(cmd, frame_idx, view, proj,
                      draw_scratch_.data(),
                      static_cast<uint32_t>(draw_scratch_.size()));
}

}  // namespace snt::voxel
