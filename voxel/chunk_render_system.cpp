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

#include <algorithm>
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
    pending_chunks_.erase(key);

    // Worker jobs only capture a material-data snapshot, so dropping an
    // unfinished Future is safe. It also prevents an unloaded chunk from
    // uploading a stale mesh after its background remesh completes.
    std::erase_if(pending_remeshes_, [&key](const PendingRemesh& pending) {
        return pending.key == key;
    });
}

// ---------------------------------------------------------------------------
// Phase 1: schedule worker remeshes, then upload completed results
// ---------------------------------------------------------------------------

void ChunkRenderSystem::update(snt::ecs::World& /*world*/, float /*dt*/) {
    if (!renderer_ || !registry_) {
        return;
    }

    // Uploads must remain on the render/main thread. Scheduling afterwards
    // lets a serial fallback JobSystem complete and upload in the same tick.
    upload_ready_remeshes();
    schedule_dirty_remeshes();
}

void ChunkRenderSystem::schedule_dirty_remeshes() {
    uint32_t jobs_scheduled = 0;
    auto dirty_it = dirty_chunks_.begin();

    while (dirty_it != dirty_chunks_.end() &&
           jobs_scheduled < remesh_jobs_per_frame_) {
        const snt::data::ChunkKey key = *dirty_it;

        // A subsequent mark_dirty() while this key is pending deliberately
        // stays in dirty_chunks_. Once this job uploads, it schedules a fresh
        // snapshot instead of losing the newer terrain edit.
        if (pending_chunks_.contains(key)) {
            ++dirty_it;
            continue;
        }

        const snt::data::ChunkData* chunk = registry_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (!chunk) {
            SNT_LOG_WARN("ChunkRenderSystem: chunk (%d,%d,%d) '%s' missing from registry",
                         key.chunk_x, key.chunk_y, key.chunk_z, key.dimension_id.c_str());
            dirty_it = dirty_chunks_.erase(dirty_it);
            continue;
        }

        // ChunkRegistry is main-thread-only. Copy every input required by the
        // pure greedy mesher before submitting work to the job system.
        const auto materials = extract_materials(chunk->terrain);
        const int32_t sx = chunk->terrain.size_x;
        const int32_t sy = chunk->terrain.size_y;
        const int32_t sz = chunk->terrain.size_z;

        const auto future = snt::core::default_job_system().submit_future<RemeshResult>(
            [key, materials = std::move(materials), sx, sy, sz,
             air_material = air_material_, ladder_material = ladder_material_,
             transparent_mask = transparent_mask_]() -> RemeshResult {
                // Neighbor capture is a future streaming concern. An empty
                // set intentionally emits boundary faces for unloaded peers.
                NeighborMaterials neighbors;
                return RemeshResult{
                    key,
                    build_greedy_mesh(materials, sx, sy, sz, air_material,
                                      ladder_material, transparent_mask, neighbors),
                    true,
                };
            });

        pending_chunks_.insert(key);
        pending_remeshes_.push_back(PendingRemesh{key, future});
        dirty_it = dirty_chunks_.erase(dirty_it);
        ++jobs_scheduled;
    }
}

void ChunkRenderSystem::upload_ready_remeshes() {
    uint32_t uploads = 0;
    auto pending_it = pending_remeshes_.begin();

    while (pending_it != pending_remeshes_.end() && uploads < uploads_per_frame_) {
        if (!pending_it->future.is_ready()) {
            ++pending_it;
            continue;
        }

        RemeshResult result = pending_it->future.get();
        pending_chunks_.erase(pending_it->key);
        pending_it = pending_remeshes_.erase(pending_it);
        upload_remesh_result(std::move(result));
        ++uploads;
    }
}

void ChunkRenderSystem::upload_remesh_result(RemeshResult&& result) {
    if (!result.ok) {
        SNT_LOG_ERROR("ChunkRenderSystem: remesh failed for chunk (%d,%d,%d) '%s'",
                      result.key.chunk_x, result.key.chunk_y, result.key.chunk_z,
                      result.key.dimension_id.c_str());
        return;
    }

    // Release any previously uploaded mesh before uploading the new one.
    auto it = uploaded_meshes_.find(result.key);
    if (it != uploaded_meshes_.end()) {
        renderer_->unload_mesh(it->second);
        uploaded_meshes_.erase(it);
    }

    // Skip empty meshes (fully air chunks) — no draw needed.
    if (result.mesh.vertices.empty() || result.mesh.indices.empty()) {
        return;
    }

    auto upload_r = renderer_->upload_mesh(result.mesh);
    if (!upload_r) {
        SNT_LOG_ERROR("ChunkRenderSystem: upload_mesh failed for chunk (%d,%d,%d): %s",
                      result.key.chunk_x, result.key.chunk_y, result.key.chunk_z,
                      upload_r.error().format().c_str());
        return;
    }
    uploaded_meshes_[result.key] = *upload_r;
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
