// Read-only JSON document contract implemented by the Zig engine core.
//
// Parsing copies all required source bytes into document-owned storage. Value
// handles and returned string views are borrowed from that document and remain
// valid until snt_json_document_destroy; callers must not retain them longer.
// The ABI deliberately exposes no std.json, allocator, or C++ JSON types.

#pragma once

#include "abi/abi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SntJsonDocument SntJsonDocument;
typedef struct SntJsonValue SntJsonValue;

typedef uint32_t SntJsonValueType;
enum {
    SNT_JSON_VALUE_TYPE_INVALID = 0u,
    SNT_JSON_VALUE_TYPE_NULL = 1u,
    SNT_JSON_VALUE_TYPE_BOOL = 2u,
    SNT_JSON_VALUE_TYPE_INTEGER = 3u,
    SNT_JSON_VALUE_TYPE_FLOAT = 4u,
    SNT_JSON_VALUE_TYPE_NUMBER_STRING = 5u,
    SNT_JSON_VALUE_TYPE_STRING = 6u,
    SNT_JSON_VALUE_TYPE_ARRAY = 7u,
    SNT_JSON_VALUE_TYPE_OBJECT = 8u,
};

// Parses one complete UTF-8 JSON document. The source does not need to remain
// alive after this call returns. Duplicate object keys are rejected to avoid
// ambiguous configuration. On failure, *out_document is set to null.
SntAbiStatus snt_json_document_parse(
    SntAbiByteView json_utf8,
    SntJsonDocument** out_document);

// Releases all document-owned values and strings. Null is allowed.
void snt_json_document_destroy(SntJsonDocument* document);

// Returns the immutable root value borrowed from document.
SntAbiStatus snt_json_document_root(
    const SntJsonDocument* document,
    const SntJsonValue** out_root);

// Reads one value's type. All scalar/object/array accessors below require the
// matching JSON type and return SNT_ABI_STATUS_INVALID_ARGUMENT otherwise.
SntAbiStatus snt_json_value_query_type(
    const SntJsonValue* value,
    SntJsonValueType* out_type);
SntAbiStatus snt_json_value_read_bool(
    const SntJsonValue* value,
    uint32_t* out_bool);
SntAbiStatus snt_json_value_read_int64(
    const SntJsonValue* value,
    int64_t* out_value);
SntAbiStatus snt_json_value_read_uint64(
    const SntJsonValue* value,
    uint64_t* out_value);
SntAbiStatus snt_json_value_read_float64(
    const SntJsonValue* value,
    double* out_value);
SntAbiStatus snt_json_value_read_string(
    const SntJsonValue* value,
    SntAbiByteView* out_utf8);

// Looks up one object field. A missing key is normal: this returns OK and
// writes null to *out_value. The returned handle remains borrowed from the
// same document as object.
SntAbiStatus snt_json_object_find(
    const SntJsonValue* object,
    SntAbiByteView key_utf8,
    const SntJsonValue** out_value);

// Queries immutable array elements. array_get rejects out-of-range indexes.
SntAbiStatus snt_json_array_count(
    const SntJsonValue* array,
    uint64_t* out_count);
SntAbiStatus snt_json_array_get(
    const SntJsonValue* array,
    uint64_t index,
    const SntJsonValue** out_element);

#ifdef __cplusplus
}  // extern "C"
#endif
