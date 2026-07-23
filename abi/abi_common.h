// Shared C-compatible primitives for native host boundaries.
//
// This header intentionally avoids C++ standard-library types and layout
// assumptions outside the C ABI. Every ABI-facing struct starts with an
// explicit struct_size field when it may grow in a later revision.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SNT_ABI_VERSION_MAJOR 1u
#define SNT_ABI_VERSION_MINOR 3u

typedef uint32_t SntAbiStatus;
enum {
    SNT_ABI_STATUS_OK = 0u,
    SNT_ABI_STATUS_INVALID_ARGUMENT = 1u,
    SNT_ABI_STATUS_INCOMPATIBLE_VERSION = 2u,
    SNT_ABI_STATUS_UNSUPPORTED = 3u,
    SNT_ABI_STATUS_INTERNAL_ERROR = 4u,
    SNT_ABI_STATUS_NOT_READY = 5u,
    SNT_ABI_STATUS_INVALID_STATE = 6u,
};

// A read-only contiguous byte range. The producer owns its storage and must
// document the range's lifetime at the operation that returns it.
typedef struct SntAbiByteView {
    const uint8_t* data;
    uint64_t size_bytes;
} SntAbiByteView;

typedef struct SntAbiMutableByteView {
    uint8_t* data;
    uint64_t size_bytes;
} SntAbiMutableByteView;

typedef uint32_t SntAbiLogSeverity;
enum {
    SNT_ABI_LOG_TRACE = 0u,
    SNT_ABI_LOG_DEBUG = 1u,
    SNT_ABI_LOG_INFO = 2u,
    SNT_ABI_LOG_WARN = 3u,
    SNT_ABI_LOG_ERROR = 4u,
    SNT_ABI_LOG_FATAL = 5u,
};

// Reserved for host lifecycle and aggregated diagnostic events. Implementers
// must never invoke this callback from per-frame, per-entity, or per-voxel
// paths; high-frequency observability belongs in counters and snapshots.
typedef void (*SntAbiLogCallback)(void* user_data,
                                  SntAbiLogSeverity severity,
                                  const char* channel,
                                  const char* message);

// Returns a static, immutable description for a status value. The returned
// pointer is owned by the ABI implementation and never needs freeing.
const char* snt_abi_status_message(SntAbiStatus status);

#ifdef __cplusplus
}  // extern "C"
#endif
