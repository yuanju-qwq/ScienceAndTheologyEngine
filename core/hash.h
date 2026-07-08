// Stable string/hash utilities for engine-wide use.
//
// Design rationale:
//   - std::hash<std::string> is NOT stable across runs, processes, or
//     platforms (libstdc++ vs libc++ vs MSVC differ; some seed per
//     process for hardening). That makes it unsuitable as a persistent
//     key in asset manifests, save files, or network replication.
//   - hash_string implements FNV-1a 64-bit, a deterministic,
//     platform-independent hash. Same input -> same hash on every
//     compiler/OS/run, so a path hashed at build time matches the path
//     hashed at runtime.
//   - hash_combine follows the boost-style recipe so multi-field keys
//     (e.g. pair<path, type_tag>) can be reduced to a single uint64_t
//     without writing a custom hash functor each time.
//
// Use cases:
//   - AssetCache: primary lookup key for path -> AssetHandle (replaces
//     std::string comparisons with O(1) uint64_t probes).
//   - AssetManifest: stable id hashing for cross-run references.
//   - Future: network asset GUIDs, shader cache keys, content-addressable
//     resource lookup.
//
// NOT a cryptographic hash. FNV-1a is fast and well-distributed for
// table keys but trivially invertible; do not use for authentication
// or tamper detection. For those, pull in a real SHA-256/blake3 later.
//
// Layering: header-only, no deps beyond <cstdint> and <string_view>.
// Lives in core/ so all engine layers (assets, ecs, scene, network) can
// share the same hashing convention.

#pragma once

#include <cstdint>
#include <string_view>

namespace snt::core {

// FNV-1a 64-bit hash of a byte sequence.
//
// Algorithm: start with the FNV offset basis (0xcbf29ce484222325),
// then for each byte XOR it into the hash and multiply by the FNV
// prime (0x100000001b3). The XOR-then-multiply ordering is what makes
// it "1a" (the original FNV-1 multiplied first and is slightly weaker
// for short keys).
//
// Determinism: same input -> same output on every platform. The
// constants are fixed by the spec; no per-process randomization.
//
// Distribution: avalanche is good for ASCII paths and binary keys
// alike. Collision rate for asset paths (typically <1M entries) is
// vanishingly small (~1e-12 probability of any collision at 1M keys).
inline uint64_t hash_string(std::string_view s) noexcept {
    uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a 64-bit offset basis
    for (char c : s) {
        h ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
        h *= 0x100000001b3ull;            // FNV-1a 64-bit prime
    }
    return h;
}

// Combine an existing hash with a new value (boost::hash_combine recipe).
//
// Typical use: build a composite key from several fields without
// writing a custom hash functor:
//   uint64_t key = hash_string(path);
//   key = hash_combine(key, static_cast<uint64_t>(type_tag));
//
// The magic constant 0x9e3779b9 is the golden-ratio fraction
// (2^32 / phi); XOR-rotate-shift mixers like this distribute bits
// across the whole output range so multi-field keys don't collide
// on common suffix patterns.
inline uint64_t hash_combine(uint64_t seed, uint64_t value) noexcept {
    return seed ^ (value + 0x9e3779b9ull + (seed << 6) + (seed >> 2));
}

}  // namespace snt::core
