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

// ===========================================================================
// Standard gameplay components (P2 task 5).
//
// These are data-only structs following the ECS philosophy. Systems query
// them via the World (EnTT registry) and mutate them each tick. Keeping
// them separate from the render-side components (Transform/MeshRef/Camera)
// makes gameplay logic testable without a renderer.
// ===========================================================================

// Position: world-space integer block position for voxel-world entities.
// Uses int32 for stable voxel coordinates (no float drift at large coords).
// Render-side Transform (float) is derived from this by a sync system.
struct Position {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

// Velocity: per-tick movement delta in blocks/tick.
// MovementSystem integrates this into Position each tick.
struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
};

// Health: current/max health for damageable entities.
struct Health {
    float current = 1.0f;
    float maximum = 1.0f;

    bool is_dead() const { return current <= 0.0f; }
    float fraction() const {
        return maximum > 0.0f ? current / maximum : 0.0f;
    }
};

// Inventory: simple item slot list. Item resolution is by string key;
// the item registry (data layer) maps keys to item definitions.
struct Inventory {
    struct Slot {
        std::string item_key;
        int32_t count = 0;
    };
    std::vector<Slot> slots;
    int32_t max_slots = 16;
};

// Tag components (marker-only, no data).

// PlayerMarker: marks the entity as the local player.
struct PlayerMarker {};

// CreatureMarker: marks the entity as an AI-driven creature.
struct CreatureMarker {};

// StaticMarker: marks the entity as non-moving (e.g., a placed machine).
struct StaticMarker {};

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

// Position: 3 int32s (x, y, z). Written as a raw block (12 bytes).
template <>
struct Serializer<snt::ecs::Position> {
    static void write(BinaryWriter& w, const snt::ecs::Position& p) {
        w.write_i32(p.x);
        w.write_i32(p.y);
        w.write_i32(p.z);
    }
    static bool read(BinaryReader& r, snt::ecs::Position& p) {
        return r.read_i32(p.x) &&
               r.read_i32(p.y) &&
               r.read_i32(p.z);
    }
};

// Velocity: 3 floats (vx, vy, vz).
template <>
struct Serializer<snt::ecs::Velocity> {
    static void write(BinaryWriter& w, const snt::ecs::Velocity& v) {
        w.write_f32(v.vx);
        w.write_f32(v.vy);
        w.write_f32(v.vz);
    }
    static bool read(BinaryReader& r, snt::ecs::Velocity& v) {
        return r.read_f32(v.vx) &&
               r.read_f32(v.vy) &&
               r.read_f32(v.vz);
    }
};

// Health: 2 floats (current, maximum).
template <>
struct Serializer<snt::ecs::Health> {
    static void write(BinaryWriter& w, const snt::ecs::Health& h) {
        w.write_f32(h.current);
        w.write_f32(h.maximum);
    }
    static bool read(BinaryReader& r, snt::ecs::Health& h) {
        return r.read_f32(h.current) &&
               r.read_f32(h.maximum);
    }
};

// Inventory: slot count + each slot (key string + count).
template <>
struct Serializer<snt::ecs::Inventory> {
    static void write(BinaryWriter& w, const snt::ecs::Inventory& inv) {
        w.write_i32(inv.max_slots);
        w.write_u32(static_cast<uint32_t>(inv.slots.size()));
        for (const auto& slot : inv.slots) {
            w.write_string(slot.item_key);
            w.write_i32(slot.count);
        }
    }
    static bool read(BinaryReader& r, snt::ecs::Inventory& inv) {
        if (!r.read_i32(inv.max_slots)) return false;
        uint32_t slot_count = 0;
        if (!r.read_u32(slot_count)) return false;
        inv.slots.clear();
        inv.slots.reserve(slot_count);
        for (uint32_t i = 0; i < slot_count; ++i) {
            snt::ecs::Inventory::Slot slot;
            if (!r.read_string(slot.item_key)) return false;
            if (!r.read_i32(slot.count)) return false;
            inv.slots.push_back(std::move(slot));
        }
        return true;
    }
};

}  // namespace snt::core
