// EntityGuid — stable, persistent entity identifier independent of entt::entity.
//
// Why a separate Guid?
//   - entt::entity is a recycled handle: after destroy + create, the same
//     numeric value can refer to a different logical entity. Saving/loading
//     a scene by entt::entity would silently splice unrelated state.
//   - EntityGuid is monotonic and never reused. It is the canonical
//     identity used by: scene serialization, save files, network
//     replication, scripting (AngelScript sees Guids, not entt handles).
//   - Stored as an ECS component on each entity (see World::create_entity
//     _with_guid). The reverse lookup (Guid -> entt::entity) is maintained
//     automatically by World via entt's on_construct/on_destroy observers,
//     so callers never have to sync two structures by hand.
//
// Layout: uint64_t. Low 32 bits are a counter; high 32 bits are a process-
// unique seed set at EntityGuidGenerator construction (currently derived
// from a steady_clock tick + a random twiddle, so two saves loaded in the
// same process don't collide on Guids that share the low counter).
//
// Serialization: trivially copyable, so it can be written to disk with a
// single write_u64 in the binary scene format. See core/binary_writer.h.

#pragma once

#include <cstdint>
#include <functional>  // std::hash

namespace snt::ecs {

// 64-bit stable entity identifier. Never reused within a generator's
// lifetime. 0 is reserved as the invalid sentinel.
struct EntityGuid {
    uint64_t value = 0;

    constexpr bool valid() const { return value != 0; }

    friend bool operator==(const EntityGuid&, const EntityGuid&) = default;
    friend bool operator!=(const EntityGuid&, const EntityGuid&) = default;
};

// Sentinel for "no entity". Mirrors entt::null semantics for Guids.
constexpr EntityGuid kInvalidEntityGuid{0};

// ---------------------------------------------------------------------------
// EntityGuidGenerator
// ---------------------------------------------------------------------------
// Issues monotonically increasing EntityGuids. The high 32 bits are a
// process-unique seed so Guids issued by two generators in the same
// process never collide (e.g. two scene loads in the same engine run).
//
// Thread-safety: NOT thread-safe. Creation happens on the main thread in
// World; if worker threads ever need to create entities they must go
// through a thread-safe creation queue (future P3+ concern).
class EntityGuidGenerator {
public:
    // Constructs a generator with a fresh seed. The seed is derived from
    // steady_clock + an atomic-ish twiddle so two generators constructed
    // back-to-back in the same process get different seeds.
    EntityGuidGenerator();

    // Returns the next Guid. Never returns kInvalidEntityGuid (0).
    EntityGuid next();

    // Peek the next Guid without consuming it. Used by tests + scene
    // loaders that need to pre-validate reserved ids.
    EntityGuid peek_next() const;

    // Reset the counter to start at `first`. The seed is preserved.
    // Used by scene loaders that want Guids to start from a specific
    // value (e.g. resume from a saved counter). Must only be called
    // before any entity is created.
    //
    // WARNING: callers MUST ensure `first` is greater than any counter
    // value already issued by this generator. Resetting backward would
    // cause next() to re-issue Guids that already exist (e.g. in a save
    // file). The generator does NOT enforce this — it trusts the caller
    // (typically a save loader that reads the next-counter from disk).
    void reset_counter(uint32_t first);

private:
    // Seed occupies the high 32 bits; counter occupies the low 32 bits.
    // Layout: (seed << 32) | (counter & 0xFFFFFFFF).
    uint64_t seed_  = 0;
    uint32_t counter_ = 0;

    // Pack seed + counter into a Guid. Counter 0 is reserved as invalid
    // (kInvalidEntityGuid), so the first issued Guid uses counter=1.
    EntityGuid pack() const;
};

}  // namespace snt::ecs

// std::hash specialization so EntityGuid can be used as a key in
// unordered_map / unordered_set (e.g. World's Guid->entity reverse map).
template <>
struct std::hash<snt::ecs::EntityGuid> {
    size_t operator()(const snt::ecs::EntityGuid& g) const noexcept {
        return std::hash<uint64_t>{}(g.value);
    }
};

// ===========================================================================
// Serializer specialization for EntityGuid.
// ===========================================================================
// Trivially copyable, so a single write_u64 / read_u64 pair suffices.
// Defined here so that adding a field to EntityGuid immediately breaks
// the build (the serialization must be updated in lockstep).
namespace snt::core {

class BinaryWriter;
class BinaryReader;

template <typename T> struct Serializer;

template <>
struct Serializer<snt::ecs::EntityGuid> {
    static void write(BinaryWriter& w, const snt::ecs::EntityGuid& g);
    static bool read(BinaryReader& r, snt::ecs::EntityGuid& g);
};

}  // namespace snt::core
