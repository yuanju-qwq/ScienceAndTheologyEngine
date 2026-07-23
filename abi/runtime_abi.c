// Runtime ABI discovery implementation.
//
// This must remain a pure-C leaf: descriptor discovery is useful to tools and
// Zig hosts before a C++ runtime exists.

#include "abi/runtime_abi.h"

#include <stddef.h>

static int snt_abi_field_fits(uint32_t supplied_size,
                              size_t field_offset,
                              size_t field_size) {
    const size_t available_size = (size_t)supplied_size;
    return available_size >= field_offset && available_size - field_offset >= field_size;
}

static int snt_runtime_abi_supports_descriptor_prefix(uint32_t supplied_size) {
    return snt_abi_field_fits(
        supplied_size,
        offsetof(SntRuntimeAbiDescriptor, runtime_descriptor_size),
        sizeof(((SntRuntimeAbiDescriptor*)0)->runtime_descriptor_size));
}

const char* snt_abi_status_message(SntAbiStatus status) {
    switch (status) {
    case SNT_ABI_STATUS_OK:
        return "ok";
    case SNT_ABI_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case SNT_ABI_STATUS_INCOMPATIBLE_VERSION:
        return "incompatible ABI version";
    case SNT_ABI_STATUS_UNSUPPORTED:
        return "unsupported operation";
    case SNT_ABI_STATUS_INTERNAL_ERROR:
        return "internal ABI error";
    case SNT_ABI_STATUS_NOT_READY:
        return "operation not ready";
    case SNT_ABI_STATUS_INVALID_STATE:
        return "invalid runtime state";
    default:
        return "unknown ABI status";
    }
}

SntAbiStatus snt_runtime_abi_query_descriptor(
    SntRuntimeAbiDescriptor* out_descriptor) {
    const uint32_t caller_size = out_descriptor != 0 ? out_descriptor->struct_size : 0u;
    if (out_descriptor == 0 || !snt_runtime_abi_supports_descriptor_prefix(caller_size)) {
        return SNT_ABI_STATUS_INVALID_ARGUMENT;
    }

    out_descriptor->abi_major = SNT_RUNTIME_ABI_MAJOR;
    out_descriptor->abi_minor = SNT_RUNTIME_ABI_MINOR;
    out_descriptor->runtime_descriptor_size = (uint32_t)sizeof(SntRuntimeAbiDescriptor);
    if (snt_abi_field_fits(caller_size,
                           offsetof(SntRuntimeAbiDescriptor, capabilities),
                           sizeof(((SntRuntimeAbiDescriptor*)0)->capabilities))) {
        out_descriptor->capabilities =
            SNT_RUNTIME_ABI_CAPABILITY_DESCRIPTOR_QUERY |
            SNT_RUNTIME_ABI_CAPABILITY_HASH_FNV1A64 |
            SNT_RUNTIME_ABI_CAPABILITY_HOST_LIFECYCLE |
            SNT_RUNTIME_ABI_CAPABILITY_DETERMINISTIC_COMMANDS |
            SNT_RUNTIME_ABI_CAPABILITY_RENDER_SNAPSHOT_LEASES |
            SNT_RUNTIME_ABI_CAPABILITY_RUNTIME_KEY_INDEX_SNAPSHOTS |
            SNT_RUNTIME_ABI_CAPABILITY_UUID_GENERATOR;
    }
    return SNT_ABI_STATUS_OK;
}
