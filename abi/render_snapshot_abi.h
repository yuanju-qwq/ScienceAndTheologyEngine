// Value-only render snapshot contract for future non-C++ clients.
//
// A producer will later serialize presentation data into payload using a
// negotiated schema_version. This header deliberately carries no EnTT,
// Vulkan, asset-manager, or game component type, so a Mach client can consume
// snapshots without inheriting the current C++ renderer implementation.

#pragma once

#include "abi/abi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SNT_RENDER_SNAPSHOT_SCHEMA_VERSION 1u

typedef struct SntRenderSnapshotView {
    uint32_t struct_size;
    uint32_t schema_version;
    uint64_t simulation_tick;
    uint64_t presentation_sequence;
    SntAbiByteView payload;
} SntRenderSnapshotView;

#define SNT_RENDER_SNAPSHOT_VIEW_INIT \
    { (uint32_t)sizeof(SntRenderSnapshotView), SNT_RENDER_SNAPSHOT_SCHEMA_VERSION, \
      UINT64_C(0), UINT64_C(0), { 0, UINT64_C(0) } }

#ifdef __cplusplus
}  // extern "C"
#endif
