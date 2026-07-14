// Scene: binary-format scene serialization + loading.
//
// Design rationale:
//   - Scene files are binary (not JSON) per project_memory: "JSON for
//     config, binary for large data". Scenes reference many entities +
//     components, so they qualify as "large data".
//   - The format uses a 4-byte magic + 4-byte version header so future
//     format changes can be detected and rejected cleanly. See
//     SceneFileHeader + kSceneMagic + kSceneVersion.
//   - Entities are stored as a flat array of records: each record has
//     an EntityGuid + a component list. Components are self-describing
//     (type-id + payload) so the loader can skip unknown component
//     types (forward compatibility) instead of failing the whole load.
//   - MeshRef handles are stored as a u32 id. The scene loader resolves the
//     id to a source request through IMeshAssetReferenceResolver when saving,
//     and asks the same resolver for a stable handle when loading. Runtime
//     resolution may also ensure the referenced mesh is GPU-resident.
//
// File layout (all little-endian):
//   [header]
//     4 bytes magic       = "SNTS" (kSceneMagic)
//     u32   version       = kSceneVersion (1)
//     u32   entity_count
//   [entities] (entity_count times)
//     u64 entity_guid
//     u32 component_count
//     [components] (component_count times)
//       u32 component_type_id   (see ComponentTypeId enum)
//       u32 payload_size        (bytes of payload that follow)
//       [payload]               (payload_size bytes; format per type)
//
// Mesh reference boundary:
//   save_scene/load_scene depend only on IMeshAssetReferenceResolver, so scene
//   code remains independent of Vulkan and offline tools can use the
//   source-free MeshAssetReferenceRegistry.
//
// Layering: depends on assets/ (mesh reference boundary), core/
// (BinaryReader/Writer, Serializer, Expected), ecs/ (World), and render/
// (presentation components). Scene code does not call Vulkan functions.

// Logging channel for scene save/load diagnostics. Defined before
// core/log.h is included so the SNT_LOG_* macros pick up "scene".
// The #ifndef guard matches log.h's own guard, so a .cpp that includes
// scene.h after defining its own SNT_LOG_CHANNEL keeps its channel.
// (This is the header-only-module convention; see core/log.h docs.)
#ifndef SNT_LOG_CHANNEL
#  define SNT_LOG_CHANNEL "scene"
#endif

#pragma once

#include "assets/mesh_asset_reference_registry.h"
#include "core/binary_reader.h"
#include "core/binary_writer.h"
#include "core/expected.h"
#include "core/log.h"               // SNT_LOG_ERROR/WARN in detail
#include "core/serializer.h"
#include "render/render_components.h"  // Transform/MeshRef/Camera + their serializers
#include "ecs/entity_guid.h"
#include "ecs/world.h"

#include <cstdint>
#include <cstring>   // std::memcmp
#include <fstream>
#include <string>
#include <vector>

namespace snt::scene {

// Magic bytes at the start of every scene file: "SNTS" (ScienceAndTheology
// Scene). Read as 4 raw bytes (not as a u32) so endianness doesn't matter
// for the magic check.
constexpr char kSceneMagic[4] = {'S', 'N', 'T', 'S'};

// Current scene file format version. Bump when the on-disk layout changes;
// readers reject unknown versions instead of attempting partial reads.
constexpr uint32_t kSceneVersion = 1;

// Component type ids written into the scene file. These are stable across
// builds (never renumber existing ids); new components get the next free
// id. The id space is intentionally small + dense so the loader can use
// a switch instead of a hash table.
enum class ComponentTypeId : uint32_t {
    Transform = 1,
    MeshRef   = 2,
    Camera    = 3,
    // Add new component types here. Never reuse ids.
};

// File header. Read/written as raw bytes via BinaryReader/Writer.
struct SceneFileHeader {
    char     magic[4];      // kSceneMagic
    uint32_t version;       // kSceneVersion
    uint32_t entity_count;  // number of entity records that follow
};

// ---------------------------------------------------------------------------
// Implementation (header-only scene serialization)
// ---------------------------------------------------------------------------
namespace detail {

using snt::core::BinaryReader;
using snt::core::BinaryWriter;
using snt::core::Serializer;
using snt::render::Camera;
using snt::ecs::EntityGuid;
using snt::render::MeshRef;
using snt::render::Transform;

// Write the file header (magic + version + entity count).
inline void write_header(BinaryWriter& w, uint32_t entity_count) {
    w.write_raw(kSceneMagic, 4);
    w.write_u32(kSceneVersion);
    w.write_u32(entity_count);
}

// Serialize one entity's components. Writes a component_count u32,
// followed by (type_id, payload_size, payload) triples for each
// component the entity has. Unknown components are silently skipped
// (the save format only knows Transform/MeshRef/Camera for now).
inline void write_entity(
    BinaryWriter& w,
    const snt::ecs::World& world,
    entt::entity e,
    const snt::assets::IMeshAssetReferenceResolver& mesh_references) {
    const EntityGuid guid = world.guid_of(e);
    Serializer<EntityGuid>::write(w, guid);

    // Count components we know how to serialize. We do a pre-pass to
    // count so the reader can allocate / validate the component list.
    uint32_t component_count = 0;
    if (world.registry().all_of<Transform>(e)) ++component_count;
    if (world.registry().all_of<MeshRef>(e))   ++component_count;
    if (world.registry().all_of<Camera>(e))    ++component_count;
    w.write_u32(component_count);

    // Write each known component as (type_id, payload_size, payload).
    // payload_size is written BEFORE the payload so a reader that doesn't
    // know the type can skip forward by payload_size bytes (forward compat).
    if (world.registry().all_of<Transform>(e)) {
        const auto& t = world.registry().get<Transform>(e);
        w.write_u32(static_cast<uint32_t>(ComponentTypeId::Transform));
        BinaryWriter payload;
        Serializer<Transform>::write(payload, t);
        w.write_u32(static_cast<uint32_t>(payload.size()));
        w.write_raw(payload.buffer().data(), payload.size());
    }
    if (world.registry().all_of<MeshRef>(e)) {
        const auto& m = world.registry().get<MeshRef>(e);
        w.write_u32(static_cast<uint32_t>(ComponentTypeId::MeshRef));
        // MeshRef payload is the mesh PATH (string), not the runtime
        // handle — handles are unstable across runs, so we resolve to
        // the path here and back to a handle on load.
        const std::string mesh_path = mesh_references.mesh_path(m.handle);
        BinaryWriter payload;
        payload.write_string(mesh_path);
        w.write_u32(static_cast<uint32_t>(payload.size()));
        w.write_raw(payload.buffer().data(), payload.size());
    }
    if (world.registry().all_of<Camera>(e)) {
        const auto& c = world.registry().get<Camera>(e);
        w.write_u32(static_cast<uint32_t>(ComponentTypeId::Camera));
        BinaryWriter payload;
        Serializer<Camera>::write(payload, c);
        w.write_u32(static_cast<uint32_t>(payload.size()));
        w.write_raw(payload.buffer().data(), payload.size());
    }
}

// Read + validate the file header. Returns false if the magic is wrong
// or the version is unsupported. Advances the reader past the header
// on success.
inline bool read_header(BinaryReader& r, uint32_t& out_entity_count) {
    char magic[4] = {};
    if (!r.read_raw(magic, 4)) {
        SNT_LOG_ERROR("load_scene: truncated header (magic)");
        return false;
    }
    if (std::memcmp(magic, kSceneMagic, 4) != 0) {
        SNT_LOG_ERROR("load_scene: bad magic (expected 'SNTS')");
        return false;
    }
    uint32_t version = 0;
    if (!r.read_u32(version)) {
        SNT_LOG_ERROR("load_scene: truncated header (version)");
        return false;
    }
    if (version != kSceneVersion) {
        SNT_LOG_ERROR("load_scene: unsupported version %u (expected %u)",
                      version, kSceneVersion);
        return false;
    }
    if (!r.read_u32(out_entity_count)) {
        SNT_LOG_ERROR("load_scene: truncated header (entity_count)");
        return false;
    }
    return true;
}

// Read one entity record + attach its components to `e`.
// Returns false on any parse failure (truncation / unknown component
// type with no skip path). Leaves the world partially modified on failure.
inline bool read_entity(
    BinaryReader& r,
    snt::ecs::World& world,
    entt::entity e,
    snt::assets::IMeshAssetReferenceResolver& mesh_references) {
    uint32_t component_count = 0;
    if (!r.read_u32(component_count)) {
        SNT_LOG_ERROR("load_scene: truncated entity (component_count)");
        return false;
    }
    for (uint32_t i = 0; i < component_count; ++i) {
        uint32_t type_id_raw = 0;
        if (!r.read_u32(type_id_raw)) {
            SNT_LOG_ERROR("load_scene: truncated component (type_id)");
            return false;
        }
        uint32_t payload_size = 0;
        if (!r.read_u32(payload_size)) {
            SNT_LOG_ERROR("load_scene: truncated component (payload_size)");
            return false;
        }
        // Bounds-check the payload against the remaining buffer so a
        // corrupt payload_size doesn't read past EOF.
        if (r.remaining() < payload_size) {
            SNT_LOG_ERROR("load_scene: payload_size %u exceeds remaining %zu",
                          payload_size, r.remaining());
            return false;
        }

        const auto type_id = static_cast<ComponentTypeId>(type_id_raw);
        switch (type_id) {
            case ComponentTypeId::Transform: {
                // Read payload into a sub-reader so Serializer<Transform>::read
                // can consume exactly payload_size bytes.
                std::vector<uint8_t> payload(payload_size);
                if (!r.read_raw(payload.data(), payload_size)) return false;
                BinaryReader pr{payload};
                Transform t;
                if (!Serializer<Transform>::read(pr, t)) {
                    SNT_LOG_ERROR("load_scene: Transform payload parse failed");
                    return false;
                }
                world.add_component<Transform>(e, t);
                break;
            }
            case ComponentTypeId::MeshRef: {
                std::vector<uint8_t> payload(payload_size);
                if (!r.read_raw(payload.data(), payload_size)) return false;
                BinaryReader pr{payload};
                std::string mesh_path;
                if (!pr.read_string(mesh_path)) {
                    SNT_LOG_ERROR("load_scene: MeshRef payload parse failed");
                    return false;
                }
                // Runtime resolution returns a preloaded manifest handle when
                // available and otherwise uploads the explicitly requested mesh.
                auto load_result = mesh_references.resolve_mesh(mesh_path);
                if (!load_result) {
                    snt::core::Error err = load_result.error();
                    err.with_context("load_scene (resolve_mesh '" + mesh_path + "')");
                    SNT_LOG_ERROR("%s", err.format().c_str());
                    return false;
                }
                world.add_component<MeshRef>(e, MeshRef{*load_result});
                break;
            }
            case ComponentTypeId::Camera: {
                std::vector<uint8_t> payload(payload_size);
                if (!r.read_raw(payload.data(), payload_size)) return false;
                BinaryReader pr{payload};
                Camera c;
                if (!Serializer<Camera>::read(pr, c)) {
                    SNT_LOG_ERROR("load_scene: Camera payload parse failed");
                    return false;
                }
                world.add_component<Camera>(e, c);
                break;
            }
            default:
                // Unknown component type — skip forward by payload_size
                // so future scene versions with new component types don't
                // break old readers. This is the forward-compat path.
                SNT_LOG_WARN("load_scene: unknown component type_id %u; skipping %u bytes",
                             type_id_raw, payload_size);
                if (!r.skip(payload_size)) return false;
                break;
        }
    }
    return true;
}

// Read the entire file contents into a byte buffer. Used by load_scene
// so the BinaryReader can bounds-check the whole buffer upfront.
inline bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    ifs.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    out.resize(size);
    if (size > 0) {
        ifs.read(reinterpret_cast<char*>(out.data()), size);
    }
    return true;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

inline snt::core::Expected<void> save_scene(
    const snt::ecs::World& world,
    const snt::assets::IMeshAssetReferenceResolver& mesh_references,
    const std::string& path) {
    // Collect entities with an EntityGuid component — those are the ones
    // that can be stably re-loaded.
    std::vector<entt::entity> entities;
    world.registry().view<snt::ecs::EntityGuid>().each([&](auto e, const auto&) {
        entities.push_back(e);
    });

    snt::core::BinaryWriter w;
    detail::write_header(w, static_cast<uint32_t>(entities.size()));
    for (entt::entity e : entities) {
        detail::write_entity(w, world, e, mesh_references);
    }

    // `path` is used verbatim — callers that need game-root resolution
    // should call RuntimePathResolver::resolve_game() before passing it in. This keeps
    // save_scene usable with absolute paths (e.g. temp files in tests)
    // without being forced through the engine-root prefix.
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) {
        return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                "save_scene: cannot open '" + path + "' for writing"};
    }
    if (!w.empty()) {
        ofs.write(reinterpret_cast<const char*>(w.buffer().data()),
                  w.buffer().size());
    }
    SNT_LOG_INFO("Scene saved: '%s' (%zu entities, %zu bytes)",
                 path.c_str(), entities.size(), w.size());
    return {};
}

inline snt::core::Expected<void> load_scene(
    snt::ecs::World& world,
    snt::assets::IMeshAssetReferenceResolver& mesh_references,
    const std::string& path) {
    // `path` is used verbatim (see save_scene comment for rationale).
    std::vector<uint8_t> buffer;
    if (!detail::read_file(path, buffer)) {
        return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                "load_scene: cannot open '" + path + "'"};
    }

    snt::core::BinaryReader r{buffer};
    uint32_t entity_count = 0;
    if (!detail::read_header(r, entity_count)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "load_scene: invalid or unsupported scene file"};
    }

    for (uint32_t i = 0; i < entity_count; ++i) {
        snt::ecs::EntityGuid guid;
        if (!snt::core::Serializer<snt::ecs::EntityGuid>::read(r, guid)) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "load_scene: truncated entity (guid)"};
        }
        entt::entity e = world.create_entity_with_guid(guid);
        if (e == entt::null) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "load_scene: duplicate or invalid guid in scene"};
        }
        if (!detail::read_entity(r, world, e, mesh_references)) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "load_scene: corrupt entity record"};
        }
    }

    SNT_LOG_INFO("Scene loaded: '%s' (%u entities)",
                 path.c_str(), entity_count);
    return {};
}

}  // namespace snt::scene
