// Uuid — general-purpose 128-bit unique identifier.
//
// Why a separate Uuid when EntityGuid already exists?
//   - EntityGuid is 64-bit and entity-specific: its layout packs a
//     per-generator seed (high 32 bits) + monotonic counter (low 32
//     bits), and its semantics ("stable entity identity across
//     save/load") are tied to the ECS World's reverse map.
//   - Uuid is 128-bit and resource-agnostic. It serves asset GUIDs,
//     network session ids, save-file headers, mod/content pack
//     identifiers — anything that needs a globally unique handle
//     without dragging in entity-specific assumptions.
//   - The 128-bit space (2^128) makes random Uuids effectively
//     collision-free across runs, processes, and machines without
//     coordination. This is the same property RFC 4122 UUIDs rely on.
//
// Layout: two uint64_t fields (low, high). Trivially copyable, so it
// serializes as two write_u64 calls (see Serializer<Uuid> below).
//
// Generation: UuidGenerator issues monotonic Uuids by combining a
// 128-bit random seed (high + low) with a per-generator counter. Two
// generators constructed in the same process get different seeds, so
// their Uuids never collide. The seed mixes steady_clock ticks with
// std::random_device draws (same approach as EntityGuidGenerator, but
// expanded to 128 bits).
//
// Thread-safety: NOT thread-safe. Issue from a single thread (typically
// the main thread during scene/asset loading). Worker threads that need
// Uuids must go through a thread-safe issuer queue (future P3+ concern).
//
// Layering: lives in core/ alongside EntityGuid's home (ecs/) so it can
// be used by assets/, scene/, network/, etc. without depending on the
// ECS layer.

#pragma once

#include <cstdint>
#include <functional>  // std::hash

namespace snt::core {

// 128-bit unique identifier. Never reused within a generator's
// lifetime. {0, 0} is reserved as the invalid sentinel.
struct Uuid {
    uint64_t low  = 0;
    uint64_t high = 0;

    constexpr bool valid() const { return low != 0 || high != 0; }

    friend bool operator==(const Uuid&, const Uuid&) = default;
    friend bool operator!=(const Uuid&, const Uuid&) = default;
};

// Sentinel for "no uuid". Mirrors kInvalidEntityGuid semantics.
constexpr Uuid kInvalidUuid{0, 0};

// ---------------------------------------------------------------------------
// UuidGenerator
// ---------------------------------------------------------------------------
// Issues monotonically increasing Uuids. Each generator has a unique
// 128-bit seed; the counter occupies the low 64 bits of issued Uuids
// while the seed provides the high 64 bits + part of the low bits.
//
// Rationale for 128-bit: even with a single generator issuing 1 billion
// Uuids per second, the counter (64-bit) wraps only after ~584 years.
// Combined with a per-generator seed, cross-process / cross-machine
// collisions are astronomically unlikely without coordination.
class UuidGenerator {
public:
    // Constructs a generator with a fresh 128-bit seed. The seed mixes
    // steady_clock nanos with two random_device draws so two generators
    // constructed back-to-back in the same process get different seeds.
    UuidGenerator();

    // Returns the next Uuid. Never returns kInvalidUuid ({0,0}).
    Uuid next();

    // Peek the next Uuid without consuming it. Used by tests + loaders
    // that need to pre-validate reserved ids.
    Uuid peek_next() const;

    // Reset the counter to start at `first`. The seed is preserved.
    // Used by loaders that want Uuids to resume from a saved counter.
    //
    // WARNING: callers MUST ensure `first` is greater than any counter
    // value already issued by this generator. Resetting backward would
    // re-issue Uuids that already exist (e.g. in a save file). The
    // generator does NOT enforce this — it trusts the caller.
    void reset_counter(uint64_t first);

private:
    // 128-bit seed. `seed_low_` is XOR-folded into the counter half of
    // each issued Uuid so two generators with adjacent counters still
    // produce disjoint Uuids.
    uint64_t seed_high_ = 0;
    uint64_t seed_low_  = 0;
    uint64_t counter_   = 0;  // next issued Uuid uses counter + 1

    // Pack seed + counter into a Uuid. Counter 0 is reserved as
    // invalid (kInvalidUuid), so the first issued Uuid uses counter=1.
    Uuid pack() const;
};

}  // namespace snt::core

// std::hash specialization so Uuid can be used as a key in
// unordered_map / unordered_set (e.g. asset GUID -> AssetHandle).
template <>
struct std::hash<snt::core::Uuid> {
    size_t operator()(const snt::core::Uuid& u) const noexcept {
        // boost::hash_combine recipe over the two halves.
        size_t h = std::hash<uint64_t>{}(u.low);
        h ^= std::hash<uint64_t>{}(u.high) + 0x9e3779b9ull + (h << 6) + (h >> 2);
        return h;
    }
};

// ===========================================================================
// Serializer specialization for Uuid.
// ===========================================================================
// Trivially copyable 128-bit value -> two write_u64 / read_u64 calls.
// Declared here (in snt::core) so adding a field to Uuid immediately
// breaks the build (the serialization must be updated in lockstep).
// Implementation lives in uuid.cpp to keep this header dependency-light
// (only forward-declares BinaryWriter / BinaryReader).
namespace snt::core {

class BinaryWriter;
class BinaryReader;

template <typename T> struct Serializer;

template <>
struct Serializer<snt::core::Uuid> {
    static void write(BinaryWriter& w, const snt::core::Uuid& u);
    static bool read(BinaryReader& r, snt::core::Uuid& u);
};

}  // namespace snt::core
