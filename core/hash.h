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
//   - Script loader: stable source/module lookup keys.
//   - Future: network asset GUIDs, shader cache keys, and content-addressable
//     resource lookup.
//
// NOT a cryptographic hash. FNV-1a is fast and well-distributed for
// table keys but trivially invertible; do not use for authentication
// or tamper detection. For those, pull in a real SHA-256/blake3 later.
//
// Layering: this is the C++ facade for the Zig-owned C ABI implementation.
// Lives in core/ so all engine layers (assets, ecs, scene, network) keep the
// established C++ call sites while sharing one native implementation.

#pragma once

#include "abi/hash_abi.h"

#include <cassert>
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
    const SntAbiByteView bytes{
        reinterpret_cast<const uint8_t*>(s.data()),
        static_cast<uint64_t>(s.size()),
    };
    uint64_t hash = 0;
    const SntAbiStatus status = snt_hash_abi_fnv1a64(bytes, &hash);

    // A valid std::string_view always satisfies the C ABI's pointer/length
    // contract. Keeping this assertion makes an accidental future contract
    // violation visible without retaining a second C++ implementation.
    assert(status == SNT_ABI_STATUS_OK);
    return hash;
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
    return snt_hash_abi_combine(seed, value);
}

}  // namespace snt::core
