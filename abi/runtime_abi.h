// Runtime ABI discovery contract.
//
// This is the entry point for language-neutral hosts. It intentionally does
// not create a SimulationRuntime yet: game-owned ISimulationSession creation
// has no settled C value contract. Establishing discovery and value-only
// snapshot contracts first prevents C++ ownership from leaking into Zig.

#pragma once

#include "abi/abi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SNT_RUNTIME_ABI_MAJOR 1u
#define SNT_RUNTIME_ABI_MINOR 0u

typedef uint64_t SntRuntimeAbiCapabilities;
enum {
    SNT_RUNTIME_ABI_CAPABILITY_DESCRIPTOR_QUERY = UINT64_C(1) << 0,
};

// The caller initializes struct_size to its allocated byte count. The ABI
// writes only fields that fit in that count, allowing trailing fields to be
// appended in a later ABI minor version without overwriting older callers.
typedef struct SntRuntimeAbiDescriptor {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t runtime_descriptor_size;
    SntRuntimeAbiCapabilities capabilities;
    uint64_t reserved[3];
} SntRuntimeAbiDescriptor;

#define SNT_RUNTIME_ABI_DESCRIPTOR_INIT \
    { (uint32_t)sizeof(SntRuntimeAbiDescriptor), 0u, 0u, 0u, UINT64_C(0), \
      { UINT64_C(0), UINT64_C(0), UINT64_C(0) } }

// Reserved opaque host identity. Creation and tick operations are intentionally
// not declared until their game-session ownership and shutdown rules have a
// complete C value contract.
typedef struct SntRuntimeHost SntRuntimeHost;

typedef struct SntRuntimeHostCallbacks {
    uint32_t struct_size;
    void* user_data;
    SntAbiLogCallback log;
} SntRuntimeHostCallbacks;

// Queries the ABI supplied by this linked runtime. The caller must set
// out_descriptor->struct_size before invoking the function.
SntAbiStatus snt_runtime_abi_query_descriptor(SntRuntimeAbiDescriptor* out_descriptor);

#ifdef __cplusplus
}  // extern "C"
#endif
