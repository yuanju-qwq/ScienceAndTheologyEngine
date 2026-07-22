#include "abi/render_snapshot_abi.h"
#include "abi/runtime_abi.h"

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
    return snapshot.struct_size == (uint32_t)sizeof(SntRenderSnapshotView) &&
           snapshot.schema_version == SNT_RENDER_SNAPSHOT_SCHEMA_VERSION &&
           snapshot.payload.data == 0 && snapshot.payload.size_bytes == UINT64_C(0);
}
