// Runtime ABI discovery and capability negotiation contract.
//
// Host lifecycle declarations live in runtime_host_abi.h. The shipped snt_abi
// archive implements every listed host capability; consumers still negotiate
// the descriptor before use so an alternate archive can be rejected cleanly.

#pragma once

#include "abi/abi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SNT_RUNTIME_ABI_MAJOR 1u
#define SNT_RUNTIME_ABI_MINOR 4u

typedef uint64_t SntRuntimeAbiCapabilities;
enum {
    SNT_RUNTIME_ABI_CAPABILITY_DESCRIPTOR_QUERY = UINT64_C(1) << 0,
    SNT_RUNTIME_ABI_CAPABILITY_HASH_FNV1A64 = UINT64_C(1) << 1,
    SNT_RUNTIME_ABI_CAPABILITY_HOST_LIFECYCLE = UINT64_C(1) << 2,
    SNT_RUNTIME_ABI_CAPABILITY_DETERMINISTIC_COMMANDS = UINT64_C(1) << 3,
    SNT_RUNTIME_ABI_CAPABILITY_RENDER_SNAPSHOT_LEASES = UINT64_C(1) << 4,
    SNT_RUNTIME_ABI_CAPABILITY_RUNTIME_KEY_INDEX_SNAPSHOTS = UINT64_C(1) << 5,
    SNT_RUNTIME_ABI_CAPABILITY_UUID_GENERATOR = UINT64_C(1) << 6,
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

// Queries the ABI supplied by this linked runtime. The caller must set
// out_descriptor->struct_size before invoking the function.
SntAbiStatus snt_runtime_abi_query_descriptor(SntRuntimeAbiDescriptor* out_descriptor);

#ifdef __cplusplus
}  // extern "C"
#endif
