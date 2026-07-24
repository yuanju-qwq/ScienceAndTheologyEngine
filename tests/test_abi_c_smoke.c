#include "abi/hash_abi.h"
#include "abi/json_abi.h"
#include "abi/render_snapshot_abi.h"
#include "abi/runtime_abi.h"
#include "abi/runtime_host_abi.h"
#include "abi/runtime_key_index_abi.h"
#include "abi/uuid_abi.h"

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
    create_info.session_callbacks.run_fixed_systems = snt_abi_c_smoke_host_fixed_tick;
    create_info.session_callbacks.after_fixed_tick = snt_abi_c_smoke_host_fixed_tick;
    create_info.session_callbacks.shutdown = snt_abi_c_smoke_host_shutdown;
    return snt_runtime_host_create(&create_info, out_host);
}

SntAbiStatus snt_abi_c_smoke_create_runtime_key_index_contract(
    SntRuntimeKeyIndex** out_index) {
    SntRuntimeKeyIndexCreateInfo create_info = SNT_RUNTIME_KEY_INDEX_CREATE_INFO_INIT;

    if (out_index == 0) return SNT_ABI_STATUS_INVALID_ARGUMENT;
    return snt_runtime_key_index_create(&create_info, out_index);
}

SntAbiStatus snt_abi_c_smoke_uuid_generate(SntUuid* out_uuid) {
    SntUuidGeneratorEntropy entropy = SNT_UUID_GENERATOR_ENTROPY_INIT;
    SntUuidGeneratorState state = SNT_UUID_GENERATOR_STATE_INIT;
    SntAbiStatus status;

    if (out_uuid == 0) return SNT_ABI_STATUS_INVALID_ARGUMENT;

    entropy.steady_clock_ticks = UINT64_C(0x1122334455667788);
    entropy.random_words[0] = 1u;
    entropy.random_words[1] = 2u;
    entropy.random_words[2] = 3u;
    entropy.random_words[3] = 4u;

    status = snt_uuid_generator_initialize(&state, &entropy);
    if (status != SNT_ABI_STATUS_OK) return status;
    return snt_uuid_generator_next(&state, out_uuid);
}

SntAbiStatus snt_abi_c_smoke_json_read_version(uint64_t* out_version) {
    static const uint8_t kJson[] = "{\"version\":7}";
    static const uint8_t kVersionKey[] = "version";
    const SntAbiByteView source = { kJson, UINT64_C(13) };
    const SntAbiByteView version_key = { kVersionKey, UINT64_C(7) };
    SntJsonDocument* document = 0;
    const SntJsonValue* root = 0;
    const SntJsonValue* version = 0;
    SntAbiStatus status;

    if (out_version == 0) return SNT_ABI_STATUS_INVALID_ARGUMENT;

    status = snt_json_document_parse(source, &document);
    if (status == SNT_ABI_STATUS_OK) {
        status = snt_json_document_root(document, &root);
    }
    if (status == SNT_ABI_STATUS_OK) {
        status = snt_json_object_find(root, version_key, &version);
    }
    if (status == SNT_ABI_STATUS_OK && version == 0) {
        status = SNT_ABI_STATUS_INTERNAL_ERROR;
    }
    if (status == SNT_ABI_STATUS_OK) {
        status = snt_json_value_read_uint64(version, out_version);
    }
    snt_json_document_destroy(document);
    return status;
}
