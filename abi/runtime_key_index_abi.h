// Deterministic runtime StringKey-to-ID index C ABI.
//
// Zig owns all published key bytes and snapshot lifetime. Consumers receive
// opaque snapshot handles and may retain them across worker work without
// observing a later rebuild. The index owner serializes rebuild, acquire,
// restore, and destroy; immutable snapshot queries and retain/release are
// safe after a snapshot has been acquired.

#pragma once

#include "abi/abi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t SntRuntimeKeyId;
enum {
    SNT_RUNTIME_KEY_ID_INVALID = 0u,
};

typedef struct SntRuntimeKeyIndex SntRuntimeKeyIndex;
typedef struct SntRuntimeKeyIndexSnapshot SntRuntimeKeyIndexSnapshot;

// The callback is optional and used only for create/rebuild/ownership
// failures. It is never invoked for a normal key miss or from a lookup path.
typedef struct SntRuntimeKeyIndexCreateInfo {
    uint32_t struct_size;
    uint32_t reserved;
    void* user_data;
    SntAbiLogCallback log;
    uint64_t reserved_words[4];
} SntRuntimeKeyIndexCreateInfo;

#define SNT_RUNTIME_KEY_INDEX_CREATE_INFO_INIT \
    { (uint32_t)sizeof(SntRuntimeKeyIndexCreateInfo), 0u, 0, 0, \
      { UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0) } }

// Metadata for one immutable snapshot. The caller sets struct_size before
// calling query; no pointers in this struct need releasing.
typedef struct SntRuntimeKeyIndexSnapshotInfo {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t generation;
    uint64_t key_count;
} SntRuntimeKeyIndexSnapshotInfo;

#define SNT_RUNTIME_KEY_INDEX_SNAPSHOT_INFO_INIT \
    { (uint32_t)sizeof(SntRuntimeKeyIndexSnapshotInfo), 0u, \
      UINT64_C(0), UINT64_C(0) }

// Creates an empty generation-zero index. The implementation copies the
// callback values but not user_data; user_data remains caller-owned until
// snt_runtime_key_index_destroy returns.
SntAbiStatus snt_runtime_key_index_create(
    const SntRuntimeKeyIndexCreateInfo* create_info,
    SntRuntimeKeyIndex** out_index);

// Releases the index's published reference. Snapshots already acquired from
// this index remain valid until each caller releases its own handle.
void snt_runtime_key_index_destroy(SntRuntimeKeyIndex* index);

// Copies every key before returning, validates non-empty/no-NUL/unique keys,
// sorts them by bytewise lexicographic order, and publishes IDs [1, N]. A
// failed rebuild leaves the previously published snapshot unchanged. keys may
// be null only when key_count is zero.
SntAbiStatus snt_runtime_key_index_rebuild(
    SntRuntimeKeyIndex* index,
    const SntAbiByteView* keys,
    uint64_t key_count);

// Acquires the current immutable generation. The caller owns one reference
// on success and must release it exactly once. This call is serialized with
// rebuild, restore, and destroy by the index owner.
SntAbiStatus snt_runtime_key_index_acquire_snapshot(
    const SntRuntimeKeyIndex* index,
    SntRuntimeKeyIndexSnapshot** out_snapshot);

// Adds or drops one caller-owned reference. retain fails only for an invalid
// handle or reference-count exhaustion; release requires a valid owned handle.
SntAbiStatus snt_runtime_key_index_snapshot_retain(
    SntRuntimeKeyIndexSnapshot* snapshot);
void snt_runtime_key_index_snapshot_release(
    SntRuntimeKeyIndexSnapshot* snapshot);

// Replaces the currently published snapshot with a retained snapshot. It is
// intended for transactional reload rollback and does not change generation.
SntAbiStatus snt_runtime_key_index_restore_snapshot(
    SntRuntimeKeyIndex* index,
    const SntRuntimeKeyIndexSnapshot* snapshot);

SntAbiStatus snt_runtime_key_index_snapshot_query(
    const SntRuntimeKeyIndexSnapshot* snapshot,
    SntRuntimeKeyIndexSnapshotInfo* out_info);

// A successful lookup writes an ID in [1, N]. A key miss writes the invalid
// sentinel and still returns OK. key is a borrowed byte view for this call.
SntAbiStatus snt_runtime_key_index_snapshot_find_id(
    const SntRuntimeKeyIndexSnapshot* snapshot,
    SntAbiByteView key,
    SntRuntimeKeyId* out_id);

// A successful lookup returns a borrowed view into immutable snapshot storage.
// A missing/invalid ID returns { null, 0 } and still returns OK. The returned
// bytes are not NUL-terminated and remain valid until snapshot_release.
SntAbiStatus snt_runtime_key_index_snapshot_find_key(
    const SntRuntimeKeyIndexSnapshot* snapshot,
    SntRuntimeKeyId id,
    SntAbiByteView* out_key);

#ifdef __cplusplus
}  // extern "C"
#endif
