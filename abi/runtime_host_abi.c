// Contract-only implementation for the future language-neutral runtime host.
//
// The current C++ SimulationRuntime still owns an ISimulationSession directly.
// These stubs keep the newly frozen ABI linkable and reject premature use until
// a value-only adapter owns create, tick, command, and snapshot lifecycles.

#include "abi/runtime_host_abi.h"

#include <stddef.h>

static int snt_runtime_host_byte_view_is_valid(SntAbiByteView view) {
    return (view.size_bytes == UINT64_C(0) || view.data != 0) &&
           view.size_bytes <= (uint64_t)(size_t)-1;
}

static int snt_runtime_host_required_path_is_valid(SntAbiByteView path) {
    return path.size_bytes != UINT64_C(0) && snt_runtime_host_byte_view_is_valid(path);
}

static int snt_runtime_host_reserved_words_are_zero(const uint64_t* words, size_t count) {
    size_t index = 0;
    if (words == 0) return 0;
    for (; index < count; ++index) {
        if (words[index] != UINT64_C(0)) return 0;
    }
    return 1;
}

static int snt_runtime_host_path_roots_are_valid(const SntRuntimeHostPathRoots* paths) {
    return paths != 0 &&
           paths->struct_size >= (uint32_t)sizeof(SntRuntimeHostPathRoots) &&
           paths->reserved == 0u &&
           snt_runtime_host_required_path_is_valid(paths->engine_root_utf8) &&
           snt_runtime_host_required_path_is_valid(paths->game_root_utf8) &&
           snt_runtime_host_required_path_is_valid(paths->user_root_utf8);
}

static int snt_runtime_config_blob_is_valid(const SntRuntimeConfigBlob* config) {
    return config != 0 &&
           config->struct_size >= (uint32_t)sizeof(SntRuntimeConfigBlob) &&
           snt_runtime_host_byte_view_is_valid(config->payload) &&
           (config->schema_version != 0u || config->payload.size_bytes == UINT64_C(0));
}

static int snt_runtime_host_callbacks_are_valid(const SntRuntimeHostCallbacks* callbacks) {
    return callbacks != 0 &&
           callbacks->struct_size >= (uint32_t)sizeof(SntRuntimeHostCallbacks) &&
           callbacks->reserved == 0u;
}

static int snt_runtime_session_callbacks_are_valid(
    const SntRuntimeSessionCallbacks* callbacks) {
    return callbacks != 0 &&
           callbacks->struct_size >= (uint32_t)sizeof(SntRuntimeSessionCallbacks) &&
           callbacks->reserved == 0u &&
           callbacks->initialize != 0 && callbacks->before_fixed_tick != 0 &&
           callbacks->after_fixed_tick != 0 && callbacks->shutdown != 0;
}

static int snt_runtime_host_create_info_is_valid(
    const SntRuntimeHostCreateInfo* create_info) {
    return create_info != 0 &&
           create_info->struct_size >= (uint32_t)sizeof(SntRuntimeHostCreateInfo) &&
           create_info->flags == 0u &&
           snt_runtime_host_reserved_words_are_zero(create_info->reserved, 4u) &&
           create_info->fixed_tick_period_nanoseconds != UINT64_C(0) &&
           snt_runtime_host_path_roots_are_valid(&create_info->paths) &&
           snt_runtime_config_blob_is_valid(&create_info->runtime_config) &&
           snt_runtime_config_blob_is_valid(&create_info->session_config) &&
           snt_runtime_host_callbacks_are_valid(&create_info->host_callbacks) &&
           snt_runtime_session_callbacks_are_valid(&create_info->session_callbacks);
}

static int snt_runtime_command_is_valid(const SntRuntimeCommand* command) {
    return command != 0 && command->struct_size >= (uint32_t)sizeof(SntRuntimeCommand) &&
           command->command_type != 0u && command->schema_version != 0u &&
           command->flags == 0u && command->target_tick != UINT64_C(0) &&
           snt_runtime_host_byte_view_is_valid(command->payload);
}

static int snt_runtime_snapshot_publish_info_is_valid(
    const SntRenderSnapshotPublishInfo* publish_info) {
    return publish_info != 0 &&
           publish_info->struct_size >= (uint32_t)sizeof(SntRenderSnapshotPublishInfo) &&
           publish_info->schema_version != 0u &&
           snt_runtime_host_byte_view_is_valid(publish_info->payload);
}

static void snt_runtime_host_log_unavailable(
    const SntRuntimeHostCreateInfo* create_info) {
    const SntRuntimeHostCallbacks* callbacks = &create_info->host_callbacks;
    if (callbacks->log != 0) {
        callbacks->log(callbacks->user_data, SNT_ABI_LOG_WARN, "runtime_host_abi",
                       "SntRuntimeHost contract is declared but no adapter is linked");
    }
}

SntAbiStatus snt_runtime_host_create(const SntRuntimeHostCreateInfo* create_info,
                                     SntRuntimeHost** out_host) {
    if (out_host == 0) return SNT_ABI_STATUS_INVALID_ARGUMENT;
    *out_host = 0;
    if (!snt_runtime_host_create_info_is_valid(create_info)) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    if (create_info->requested_abi_major != SNT_RUNTIME_ABI_MAJOR ||
        create_info->requested_abi_minor > SNT_RUNTIME_ABI_MINOR) {
        return SNT_ABI_STATUS_INCOMPATIBLE_VERSION;
    }

    // Create is a lifecycle operation, so one explicit warning is low-frequency
    // and makes a missing adapter visible without polluting tick-time logging.
    snt_runtime_host_log_unavailable(create_info);
    return SNT_ABI_STATUS_UNSUPPORTED;
}

SntAbiStatus snt_runtime_host_query_state(const SntRuntimeHost* host,
                                          SntRuntimeHostState* out_state) {
    if (host == 0 || out_state == 0 ||
        out_state->struct_size < (uint32_t)sizeof(SntRuntimeHostState)) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    return SNT_ABI_STATUS_UNSUPPORTED;
}

SntAbiStatus snt_runtime_host_enqueue_command(SntRuntimeHost* host,
                                              const SntRuntimeCommand* command) {
    if (host == 0 || !snt_runtime_command_is_valid(command)) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    return SNT_ABI_STATUS_UNSUPPORTED;
}

SntAbiStatus snt_runtime_host_run_fixed_tick(SntRuntimeHost* host,
                                             uint64_t expected_tick,
                                             SntRuntimeFixedTickResult* out_result) {
    if (host == 0 || expected_tick == UINT64_C(0) || out_result == 0 ||
        out_result->struct_size < (uint32_t)sizeof(SntRuntimeFixedTickResult)) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    return SNT_ABI_STATUS_UNSUPPORTED;
}

SntAbiStatus snt_runtime_host_publish_render_snapshot(
    SntRuntimeHost* host,
    const SntRenderSnapshotPublishInfo* publish_info) {
    if (host == 0 || !snt_runtime_snapshot_publish_info_is_valid(publish_info)) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    return SNT_ABI_STATUS_UNSUPPORTED;
}

SntAbiStatus snt_runtime_host_acquire_render_snapshot(
    SntRuntimeHost* host,
    SntRenderSnapshotLease* out_lease) {
    if (host == 0 || out_lease == 0 ||
        out_lease->struct_size < (uint32_t)sizeof(SntRenderSnapshotLease)) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    return SNT_ABI_STATUS_UNSUPPORTED;
}

SntAbiStatus snt_runtime_host_release_render_snapshot(
    SntRuntimeHost* host,
    SntRenderSnapshotLeaseId lease_id) {
    if (host == 0 || lease_id == SNT_RENDER_SNAPSHOT_LEASE_INVALID) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }
    return SNT_ABI_STATUS_UNSUPPORTED;
}

SntAbiStatus snt_runtime_host_request_stop(SntRuntimeHost* host) {
    if (host == 0) return SNT_ABI_STATUS_INVALID_ARGUMENT;
    return SNT_ABI_STATUS_UNSUPPORTED;
}

void snt_runtime_host_shutdown(SntRuntimeHost* host) {
    (void)host;
}
