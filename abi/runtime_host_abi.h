// Value-only runtime host contract for C, C++, and Zig integration.
//
// This boundary owns neither C++ session objects nor Zig internal state.
// A concrete adapter copies create-time values, owns the opaque host, and
// invokes the supplied C callbacks at deterministic lifecycle boundaries.

#pragma once

#include "abi/abi_common.h"
#include "abi/render_snapshot_abi.h"
#include "abi/runtime_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SntRuntimeHost SntRuntimeHost;

// Lifecycle diagnostics are intentionally low frequency. The callback may be
// invoked during create and shutdown, never from a per-command or per-tick
// path. user_data remains caller-owned until shutdown returns.
typedef struct SntRuntimeHostCallbacks {
    uint32_t struct_size;
    uint32_t reserved;
    void* user_data;
    SntAbiLogCallback log;
} SntRuntimeHostCallbacks;

#define SNT_RUNTIME_HOST_CALLBACKS_INIT \
    { (uint32_t)sizeof(SntRuntimeHostCallbacks), 0u, 0, 0 }

// UTF-8 path roots used during host creation. All three roots must be nonempty
// in v1, and each byte view need not be NUL-terminated. The host copies all
// path bytes before create returns.
typedef struct SntRuntimeHostPathRoots {
    uint32_t struct_size;
    uint32_t reserved;
    SntAbiByteView engine_root_utf8;
    SntAbiByteView game_root_utf8;
    SntAbiByteView user_root_utf8;
} SntRuntimeHostPathRoots;

#define SNT_RUNTIME_HOST_PATH_ROOTS_INIT \
    { (uint32_t)sizeof(SntRuntimeHostPathRoots), 0u, \
      { 0, UINT64_C(0) }, { 0, UINT64_C(0) }, { 0, UINT64_C(0) } }

// A versioned, value-owned configuration payload. Version zero with an empty
// payload means that the corresponding configuration is intentionally absent.
typedef struct SntRuntimeConfigBlob {
    uint32_t struct_size;
    uint32_t schema_version;
    SntAbiByteView payload;
} SntRuntimeConfigBlob;

#define SNT_RUNTIME_CONFIG_BLOB_INIT \
    { (uint32_t)sizeof(SntRuntimeConfigBlob), 0u, { 0, UINT64_C(0) } }

// The producer ID and sequence form the stable per-source command identity.
// Commands are ordered lexicographically by target_tick, producer_id.high,
// producer_id.low, and sequence. Reusing an identity is rejected.
typedef struct SntRuntimeCommandProducerId {
    uint64_t high;
    uint64_t low;
} SntRuntimeCommandProducerId;

#define SNT_RUNTIME_COMMAND_PRODUCER_ID_INIT \
    { UINT64_C(0), UINT64_C(0) }

typedef struct SntRuntimeCommand {
    uint32_t struct_size;
    uint32_t command_type;
    uint32_t schema_version;
    uint32_t flags;
    uint64_t target_tick;
    SntRuntimeCommandProducerId producer_id;
    uint64_t sequence;
    SntAbiByteView payload;
} SntRuntimeCommand;

#define SNT_RUNTIME_COMMAND_INIT \
    { (uint32_t)sizeof(SntRuntimeCommand), 0u, 0u, 0u, UINT64_C(0), \
      SNT_RUNTIME_COMMAND_PRODUCER_ID_INIT, UINT64_C(0), { 0, UINT64_C(0) } }

// Context supplied for every command and fixed-tick callback. simulation_tick
// is one for the first invoked deterministic tick.
typedef struct SntRuntimeFixedTickContext {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t simulation_tick;
    uint64_t fixed_tick_period_nanoseconds;
    uint64_t command_count;
} SntRuntimeFixedTickContext;

#define SNT_RUNTIME_FIXED_TICK_CONTEXT_INIT \
    { (uint32_t)sizeof(SntRuntimeFixedTickContext), 0u, UINT64_C(0), \
      UINT64_C(0), UINT64_C(0) }

// The initialization context borrows host-owned copies of create-time values.
// Its pointers are valid only for the initialize callback; a session copies
// data it needs after initialization.
typedef struct SntRuntimeSessionInitializeContext {
    uint32_t struct_size;
    uint32_t reserved;
    SntRuntimeHost* host;
    const SntRuntimeHostPathRoots* paths;
    const SntRuntimeConfigBlob* runtime_config;
    const SntRuntimeConfigBlob* session_config;
    uint64_t fixed_tick_period_nanoseconds;
} SntRuntimeSessionInitializeContext;

#define SNT_RUNTIME_SESSION_INITIALIZE_CONTEXT_INIT \
    { (uint32_t)sizeof(SntRuntimeSessionInitializeContext), 0u, 0, 0, 0, 0, \
      UINT64_C(0) }

typedef SntAbiStatus (*SntRuntimeSessionInitializeCallback)(
    void* user_data, const SntRuntimeSessionInitializeContext* context);
typedef SntAbiStatus (*SntRuntimeSessionApplyCommandCallback)(
    void* user_data,
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context,
    const SntRuntimeCommand* command);
typedef SntAbiStatus (*SntRuntimeSessionFixedTickCallback)(
    void* user_data,
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context);
typedef void (*SntRuntimeSessionShutdownCallback)(void* user_data,
                                                   SntRuntimeHost* host);

// initialize, before_fixed_tick, after_fixed_tick, and shutdown are required.
// apply_command is optional; an adapter rejects command enqueue when it is
// absent. Callbacks must not call run_fixed_tick or shutdown reentrantly.
typedef struct SntRuntimeSessionCallbacks {
    uint32_t struct_size;
    uint32_t reserved;
    void* user_data;
    SntRuntimeSessionInitializeCallback initialize;
    SntRuntimeSessionApplyCommandCallback apply_command;
    SntRuntimeSessionFixedTickCallback before_fixed_tick;
    SntRuntimeSessionFixedTickCallback after_fixed_tick;
    SntRuntimeSessionShutdownCallback shutdown;
} SntRuntimeSessionCallbacks;

#define SNT_RUNTIME_SESSION_CALLBACKS_INIT \
    { (uint32_t)sizeof(SntRuntimeSessionCallbacks), 0u, 0, 0, 0, 0, 0, 0 }

// Create-time values for one deterministic runtime host. flags and reserved
// fields must be zero in v1. An adapter copies all byte views before create
// returns, then retains only the callback tables and their user_data pointers.
typedef struct SntRuntimeHostCreateInfo {
    uint32_t struct_size;
    uint32_t requested_abi_major;
    uint32_t requested_abi_minor;
    uint32_t flags;
    uint64_t fixed_tick_period_nanoseconds;
    SntRuntimeHostPathRoots paths;
    SntRuntimeConfigBlob runtime_config;
    SntRuntimeConfigBlob session_config;
    SntRuntimeHostCallbacks host_callbacks;
    SntRuntimeSessionCallbacks session_callbacks;
    uint64_t reserved[4];
} SntRuntimeHostCreateInfo;

#define SNT_RUNTIME_HOST_CREATE_INFO_INIT \
    { (uint32_t)sizeof(SntRuntimeHostCreateInfo), SNT_RUNTIME_ABI_MAJOR, \
      SNT_RUNTIME_ABI_MINOR, 0u, UINT64_C(0), SNT_RUNTIME_HOST_PATH_ROOTS_INIT, \
      SNT_RUNTIME_CONFIG_BLOB_INIT, SNT_RUNTIME_CONFIG_BLOB_INIT, \
      SNT_RUNTIME_HOST_CALLBACKS_INIT, SNT_RUNTIME_SESSION_CALLBACKS_INIT, \
      { UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0) } }

typedef uint32_t SntRuntimeHostLifecycleState;
enum {
    SNT_RUNTIME_HOST_LIFECYCLE_RUNNING = 1u,
    SNT_RUNTIME_HOST_LIFECYCLE_STOP_REQUESTED = 2u,
    SNT_RUNTIME_HOST_LIFECYCLE_SHUTDOWN = 3u,
};

typedef struct SntRuntimeHostState {
    uint32_t struct_size;
    SntRuntimeHostLifecycleState lifecycle_state;
    uint64_t completed_tick;
    uint64_t queued_command_count;
    uint64_t latest_snapshot_sequence;
} SntRuntimeHostState;

#define SNT_RUNTIME_HOST_STATE_INIT \
    { (uint32_t)sizeof(SntRuntimeHostState), SNT_RUNTIME_HOST_LIFECYCLE_RUNNING, \
      UINT64_C(0), UINT64_C(0), UINT64_C(0) }

typedef struct SntRuntimeFixedTickResult {
    uint32_t struct_size;
    SntRuntimeHostLifecycleState lifecycle_state;
    uint64_t completed_tick;
    uint64_t applied_command_count;
    uint64_t latest_snapshot_sequence;
} SntRuntimeFixedTickResult;

#define SNT_RUNTIME_FIXED_TICK_RESULT_INIT \
    { (uint32_t)sizeof(SntRuntimeFixedTickResult), \
      SNT_RUNTIME_HOST_LIFECYCLE_RUNNING, UINT64_C(0), UINT64_C(0), UINT64_C(0) }

// Creates a host after ABI negotiation. On any failure *out_host is set to
// null. A host feature is callable only when its descriptor capability bit is
// set; the declaration may otherwise return SNT_ABI_STATUS_UNSUPPORTED.
SntAbiStatus snt_runtime_host_create(const SntRuntimeHostCreateInfo* create_info,
                                     SntRuntimeHost** out_host);

// All v1 host API calls are serialized on the host control thread. State lets
// a producer choose target_tick == completed_tick + 1 before enqueueing.
SntAbiStatus snt_runtime_host_query_state(const SntRuntimeHost* host,
                                          SntRuntimeHostState* out_state);

// Copies command payload bytes before returning. target_tick must be nonzero;
// command_type and schema_version are game-defined nonzero values.
SntAbiStatus snt_runtime_host_enqueue_command(SntRuntimeHost* host,
                                              const SntRuntimeCommand* command);

// Runs exactly the tick named by expected_tick, which must equal the prior
// completed tick plus one. The callback order is apply_command (sorted),
// before_fixed_tick, engine fixed systems, and after_fixed_tick.
SntAbiStatus snt_runtime_host_run_fixed_tick(SntRuntimeHost* host,
                                             uint64_t expected_tick,
                                             SntRuntimeFixedTickResult* out_result);

// May be called only from after_fixed_tick. The host copies payload and stamps
// the current simulation tick plus the next presentation sequence.
SntAbiStatus snt_runtime_host_publish_render_snapshot(
    SntRuntimeHost* host,
    const SntRenderSnapshotPublishInfo* publish_info);

// Acquires the latest published snapshot. Its payload remains borrowed until
// the lease ID is released. A host may return SNT_ABI_STATUS_NOT_READY before
// the first post-tick snapshot is published.
SntAbiStatus snt_runtime_host_acquire_render_snapshot(
    SntRuntimeHost* host,
    SntRenderSnapshotLease* out_lease);
SntAbiStatus snt_runtime_host_release_render_snapshot(
    SntRuntimeHost* host,
    SntRenderSnapshotLeaseId lease_id);

// Requesting stop prevents later fixed ticks after the current callback
// boundary. shutdown releases outstanding snapshots, invokes session shutdown
// once, and invalidates host for every subsequent use.
SntAbiStatus snt_runtime_host_request_stop(SntRuntimeHost* host);
void snt_runtime_host_shutdown(SntRuntimeHost* host);

#ifdef __cplusplus
}  // extern "C"
#endif
