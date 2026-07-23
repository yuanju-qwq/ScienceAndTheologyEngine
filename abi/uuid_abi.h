// Value-only UUID generator contract implemented by the Zig engine core.
//
// A platform adapter supplies timestamp and entropy words at initialization.
// Zig owns seed mixing, state advancement, and UUID packing, so C, C++, and
// Zig callers share one deterministic generator implementation without a
// compiler-specific ABI or allocator dependency.

#pragma once

#include "abi/abi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fixed 128-bit UUID value. { 0, 0 } is the invalid sentinel.
typedef struct SntUuid {
    uint64_t low;
    uint64_t high;
} SntUuid;

#define SNT_UUID_INIT { UINT64_C(0), UINT64_C(0) }

// Platform adapters gather entropy but do not mix or retain it. The current
// C++ facade supplies steady-clock ticks plus four random-device words.
typedef struct SntUuidGeneratorEntropy {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t steady_clock_ticks;
    uint32_t random_words[4];
    uint64_t reserved_words[2];
} SntUuidGeneratorEntropy;

#define SNT_UUID_GENERATOR_ENTROPY_INIT \
    { (uint32_t)sizeof(SntUuidGeneratorEntropy), 0u, UINT64_C(0), \
      { 0u, 0u, 0u, 0u }, { UINT64_C(0), UINT64_C(0) } }

// Value-owned generator state. Callers must preserve struct_size and all
// reserved fields as zero after initialize. counter records the last issued
// counter; next and peek advance with uint64 wrap semantics and skip the one
// counter value that would pack to the invalid {0, 0} UUID sentinel.
typedef struct SntUuidGeneratorState {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t seed_high;
    uint64_t seed_low;
    uint64_t counter;
    uint64_t reserved_words[3];
} SntUuidGeneratorState;

#define SNT_UUID_GENERATOR_STATE_INIT \
    { (uint32_t)sizeof(SntUuidGeneratorState), 0u, UINT64_C(0), \
      UINT64_C(0), UINT64_C(0), \
      { UINT64_C(0), UINT64_C(0), UINT64_C(0) } }

// Initializes a state from caller-provided entropy. Both inputs must contain
// the full v1 struct prefix and zero reserved fields. No allocator is used.
SntAbiStatus snt_uuid_generator_initialize(
    SntUuidGeneratorState* state,
    const SntUuidGeneratorEntropy* entropy);

// Advances or observes the generator. On success, out_uuid receives a valid
// UUID. State callers are responsible for serializing access to one state.
SntAbiStatus snt_uuid_generator_next(
    SntUuidGeneratorState* state,
    SntUuid* out_uuid);
SntAbiStatus snt_uuid_generator_peek_next(
    const SntUuidGeneratorState* state,
    SntUuid* out_uuid);

// Sets the last-issued counter while preserving the Zig-derived seed. The
// caller is responsible for ensuring this does not reissue an existing UUID.
SntAbiStatus snt_uuid_generator_reset_counter(
    SntUuidGeneratorState* state,
    uint64_t counter);

#ifdef __cplusplus
}  // extern "C"
#endif
