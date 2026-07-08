// ECS components — data-only structs attached to entities.
//
// P1.5: Transform (position/rotation/scale) + MeshRef (path to .obj file).
// P2.4: MeshRef now stores a MeshHandle (uint32_t) into MeshCache instead
//        of a file path. This decouples components from string lookups
//        every frame + makes mesh references stable across cache rebuilds.
// P2.F: MeshHandle is now an alias for the canonical snt::assets::MeshHandle
//        (defined in assets/asset_handle.h). One handle type across the
//        engine — no more layout-duplicated ecs::MeshHandle / render::MeshHandle.
// P2+ will add: Velocity, Collider, Health, Inventory, etc.

#pragma once

#include "assets/asset_handle.h"  // MeshHandle = snt::assets::MeshHandle
#include "core/binary_reader.h"
#include "core/binary_writer.h"
#include "core/serializer.h"

#include <cstdint>
#include <string>

namespace snt::ecs {

// Transform: world-space position, rotation (Euler angles in degrees),
// and scale. Stored as separate floats for cache friendliness.
struct Transform {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};  // Euler degrees: pitch, yaw, roll
    float scale[3] = {1.0f, 1.0f, 1.0f};
};

// MeshHandle: alias for the canonical snt::assets::MeshHandle. Defined
// in assets/asset_handle.h; aliased here so ECS code can write `MeshHandle`
// without the assets:: prefix. Components stay free of render-backend
// deps (assets/ depends only on core/).
using MeshHandle = snt::assets::MeshHandle;

// MeshRef: reference to a mesh asset via an AssetManager mesh handle.
// The render system resolves the handle to a VulkanMesh* each frame.
struct MeshRef {
    MeshHandle handle;
};

// Camera: marks an entity as a camera. Only one active camera is used.
struct Camera {
    float fov = 60.0f;       // field of view in degrees
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    float aspect = 16.0f / 9.0f;  // updated by window resize
};

}  // namespace snt::ecs

// ===========================================================================
// Serializer specializations for ECS components.
// ===========================================================================
// These live in the same header as the component definitions so adding
// a new field + its serialization stay in sync. The scene system uses
// these via ComponentRegistry (see ecs/component_registry.h).
namespace snt::core {

// Transform: 9 floats (position[3] + rotation[3] + scale[3]) in that
// order. Written as a raw block for compactness (36 bytes).
template <>
struct Serializer<snt::ecs::Transform> {
    static void write(BinaryWriter& w, const snt::ecs::Transform& t) {
        w.write_raw(t.position, sizeof(t.position));
        w.write_raw(t.rotation, sizeof(t.rotation));
        w.write_raw(t.scale,     sizeof(t.scale));
    }
    static bool read(BinaryReader& r, snt::ecs::Transform& t) {
        return r.read_raw(t.position, sizeof(t.position)) &&
               r.read_raw(t.rotation, sizeof(t.rotation)) &&
               r.read_raw(t.scale,     sizeof(t.scale));
    }
};

// MeshRef: just the handle's u32 id. The scene loader resolves the id
// to a path via AssetCache::path_of() when saving, and back to a handle
// via AssetCache::register_preallocated() when loading. The serializer
// only moves bytes.
template <>
struct Serializer<snt::ecs::MeshRef> {
    static void write(BinaryWriter& w, const snt::ecs::MeshRef& m) {
        w.write_u32(m.handle.id);
    }
    static bool read(BinaryReader& r, snt::ecs::MeshRef& m) {
        return r.read_u32(m.handle.id);
    }
};

// Camera: 4 floats (fov, near, far, aspect). Written as a raw block.
template <>
struct Serializer<snt::ecs::Camera> {
    static void write(BinaryWriter& w, const snt::ecs::Camera& c) {
        w.write_f32(c.fov);
        w.write_f32(c.near_plane);
        w.write_f32(c.far_plane);
        w.write_f32(c.aspect);
    }
    static bool read(BinaryReader& r, snt::ecs::Camera& c) {
        return r.read_f32(c.fov) &&
               r.read_f32(c.near_plane) &&
               r.read_f32(c.far_plane) &&
               r.read_f32(c.aspect);
    }
};

}  // namespace snt::core
