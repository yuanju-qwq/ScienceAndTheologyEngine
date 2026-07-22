#include "abi/hash_abi.h"
#include "abi/render_snapshot_abi.h"
#include "abi/runtime_abi.h"
#include "abi/runtime_host_abi.h"

#include <stdint.h>

SntAbiStatus snt_abi_c_smoke_query(SntRuntimeAbiDescriptor* descriptor) {
    if (descriptor == 0) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }

    descriptor->struct_size = (uint32_t)sizeof(*descriptor);
    descriptor->abi_major = 0u;
    descriptor->abi_minor = 0u;
    descriptor->runtime_descriptor_size = 0u;
    descriptor->capabilities = UINT64_C(0);
    return snt_runtime_abi_query_descriptor(descriptor);
}

uint32_t snt_abi_c_smoke_snapshot_layout_is_valid(void) {
    SntRenderSnapshotView snapshot = SNT_RENDER_SNAPSHOT_VIEW_INIT;
    SntRenderSnapshotLease lease = SNT_RENDER_SNAPSHOT_LEASE_INIT;
    return snapshot.struct_size == (uint32_t)sizeof(SntRenderSnapshotView) &&
           snapshot.schema_version == SNT_RENDER_SNAPSHOT_SCHEMA_VERSION &&
           snapshot.payload.data == 0 && snapshot.payload.size_bytes == UINT64_C(0) &&
           lease.struct_size == (uint32_t)sizeof(SntRenderSnapshotLease) &&
           lease.lease_id == SNT_RENDER_SNAPSHOT_LEASE_INVALID &&
           lease.snapshot.struct_size == (uint32_t)sizeof(SntRenderSnapshotView);
}

SntAbiStatus snt_abi_c_smoke_hash(
    const char* text,
    uint64_t text_size,
    uint64_t* out_hash) {
    const SntAbiByteView bytes = {
        (const uint8_t*)text,
        text_size,
    };
    return snt_hash_abi_fnv1a64(bytes, out_hash);
}

static SntAbiStatus snt_abi_c_smoke_host_initialize(
    void* user_data,
    const SntRuntimeSessionInitializeContext* context) {
    (void)user_data;
    (void)context;
    return SNT_ABI_STATUS_OK;
}

static SntAbiStatus snt_abi_c_smoke_host_apply_command(
    void* user_data,
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context,
    const SntRuntimeCommand* command) {
    (void)user_data;
    (void)host;
    (void)context;
    (void)command;
    return SNT_ABI_STATUS_OK;
}

static SntAbiStatus snt_abi_c_smoke_host_fixed_tick(
    void* user_data,
    SntRuntimeHost* host,
    const SntRuntimeFixedTickContext* context) {
    (void)user_data;
    (void)host;
    (void)context;
    return SNT_ABI_STATUS_OK;
}

static void snt_abi_c_smoke_host_shutdown(void* user_data, SntRuntimeHost* host) {
    (void)user_data;
    (void)host;
}

SntAbiStatus snt_abi_c_smoke_create_host_contract(SntRuntimeHost** out_host) {
    static const uint8_t kEngineRoot[] = "engine";
    static const uint8_t kGameRoot[] = "game";
    static const uint8_t kUserRoot[] = "user";
    SntRuntimeHostCreateInfo create_info = SNT_RUNTIME_HOST_CREATE_INFO_INIT;

    if (out_host == 0) return SNT_ABI_STATUS_INVALID_ARGUMENT;

    create_info.fixed_tick_period_nanoseconds = UINT64_C(50000000);
    create_info.paths.engine_root_utf8.data = kEngineRoot;
    create_info.paths.engine_root_utf8.size_bytes = UINT64_C(6);
    create_info.paths.game_root_utf8.data = kGameRoot;
    create_info.paths.game_root_utf8.size_bytes = UINT64_C(4);
    create_info.paths.user_root_utf8.data = kUserRoot;
    create_info.paths.user_root_utf8.size_bytes = UINT64_C(4);
    create_info.session_callbacks.initialize = snt_abi_c_smoke_host_initialize;
    create_info.session_callbacks.apply_command = snt_abi_c_smoke_host_apply_command;
    create_info.session_callbacks.before_fixed_tick = snt_abi_c_smoke_host_fixed_tick;
    create_info.session_callbacks.after_fixed_tick = snt_abi_c_smoke_host_fixed_tick;
    create_info.session_callbacks.shutdown = snt_abi_c_smoke_host_shutdown;
    return snt_runtime_host_create(&create_info, out_host);
}
