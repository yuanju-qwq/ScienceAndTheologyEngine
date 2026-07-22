// Stable, allocation-free hashing contract implemented by the Zig engine.
//
// This ABI is suitable for C, C++, and future Zig hosts. It is deliberately
// limited to values and borrowed bytes: callers retain ownership of input and
// receive the result through an explicit output pointer.

#pragma once

#include "abi/abi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Computes the 64-bit FNV-1a hash of bytes. `bytes.data` may be null only
// when `bytes.size_bytes` is zero. The output pointer must be non-null.
SntAbiStatus snt_hash_abi_fnv1a64(SntAbiByteView bytes, uint64_t* out_hash);

// Combines two 64-bit values using the engine's stable boost-style mixer.
// Unsigned arithmetic wraps modulo 2^64 on every supported implementation.
uint64_t snt_hash_abi_combine(uint64_t seed, uint64_t value);

#ifdef __cplusplus
}  // extern "C"
#endif
