// Value-only render snapshot contract for future native clients.
//
// A producer will later serialize presentation data into payload using a
// negotiated schema_version. This header deliberately carries no EnTT,
// Vulkan, asset-manager, or game component type, so a native client can
// consume snapshots without inheriting the current C++ renderer implementation.

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

// A session publishes only its schema and value bytes. The host copies the
// bytes and stamps the authoritative tick plus a monotonic presentation
// sequence, preventing a producer from forging lifecycle metadata.
typedef struct SntRenderSnapshotPublishInfo {
    uint32_t struct_size;
    uint32_t schema_version;
    SntAbiByteView payload;
} SntRenderSnapshotPublishInfo;

#define SNT_RENDER_SNAPSHOT_PUBLISH_INFO_INIT \
    { (uint32_t)sizeof(SntRenderSnapshotPublishInfo), \
      SNT_RENDER_SNAPSHOT_SCHEMA_VERSION, { 0, UINT64_C(0) } }

typedef uint64_t SntRenderSnapshotLeaseId;
#define SNT_RENDER_SNAPSHOT_LEASE_INVALID UINT64_C(0)

// The payload in snapshot stays valid until the matching lease ID is released
// through snt_runtime_host_release_render_snapshot(). The caller must not
// retain either the view or its payload after a successful release.
typedef struct SntRenderSnapshotLease {
    uint32_t struct_size;
    uint32_t reserved;
    SntRenderSnapshotLeaseId lease_id;
    SntRenderSnapshotView snapshot;
} SntRenderSnapshotLease;

#define SNT_RENDER_SNAPSHOT_LEASE_INIT \
    { (uint32_t)sizeof(SntRenderSnapshotLease), 0u, \
      SNT_RENDER_SNAPSHOT_LEASE_INVALID, SNT_RENDER_SNAPSHOT_VIEW_INIT }

#ifdef __cplusplus
}  // extern "C"
#endif
