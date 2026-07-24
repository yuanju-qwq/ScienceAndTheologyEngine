// C++ facade implementation for the Zig-owned JSON document adapter.

#include "core/json.h"

#include "abi/json_abi.h"

#include <string>
#include <utility>

namespace snt::core {

namespace {

SntAbiByteView byte_view(std::string_view text) {
    return {
        reinterpret_cast<const uint8_t*>(text.data()),
        static_cast<uint64_t>(text.size()),
    };
}

ErrorCode error_code_for_status(SntAbiStatus status) noexcept {
    switch (status) {
    case SNT_ABI_STATUS_INVALID_STATE:
    case SNT_ABI_STATUS_NOT_READY:
        return ErrorCode::kInvalidState;
    case SNT_ABI_STATUS_INVALID_ARGUMENT:
    case SNT_ABI_STATUS_INCOMPATIBLE_VERSION:
    case SNT_ABI_STATUS_UNSUPPORTED:
        return ErrorCode::kInvalidArgument;
    default:
        return ErrorCode::kUnknown;
    }
}

Error json_error(const char* operation, SntAbiStatus status) {
    return Error{
        error_code_for_status(status),
        std::string("JSON ") + operation + ": " + snt_abi_status_message(status),
    };
}

Expected<JsonValueType> json_type_from_abi(SntJsonValueType type) {
    switch (type) {
    case SNT_JSON_VALUE_TYPE_NULL:
        return JsonValueType::kNull;
    case SNT_JSON_VALUE_TYPE_BOOL:
        return JsonValueType::kBool;
    case SNT_JSON_VALUE_TYPE_INTEGER:
        return JsonValueType::kInteger;
    case SNT_JSON_VALUE_TYPE_FLOAT:
        return JsonValueType::kFloat;
    case SNT_JSON_VALUE_TYPE_NUMBER_STRING:
        return JsonValueType::kNumberString;
    case SNT_JSON_VALUE_TYPE_STRING:
        return JsonValueType::kString;
    case SNT_JSON_VALUE_TYPE_ARRAY:
        return JsonValueType::kArray;
    case SNT_JSON_VALUE_TYPE_OBJECT:
        return JsonValueType::kObject;
    default:
        return Error{ErrorCode::kInvalidState, "JSON adapter returned an invalid value type"};
    }
}

}  // namespace

Expected<JsonValueType> JsonValue::type() const {
    SntJsonValueType type = SNT_JSON_VALUE_TYPE_INVALID;
    const SntAbiStatus status = snt_json_value_query_type(value_, &type);
    if (status != SNT_ABI_STATUS_OK) return json_error("type query failed", status);
    return json_type_from_abi(type);
}

Expected<bool> JsonValue::read_bool() const {
    uint32_t value = 0u;
    const SntAbiStatus status = snt_json_value_read_bool(value_, &value);
    if (status != SNT_ABI_STATUS_OK) return json_error("boolean read failed", status);
    return value != 0u;
}

Expected<int64_t> JsonValue::read_int64() const {
    int64_t value = 0;
    const SntAbiStatus status = snt_json_value_read_int64(value_, &value);
    if (status != SNT_ABI_STATUS_OK) return json_error("integer read failed", status);
    return value;
}

Expected<uint64_t> JsonValue::read_uint64() const {
    uint64_t value = 0;
    const SntAbiStatus status = snt_json_value_read_uint64(value_, &value);
    if (status != SNT_ABI_STATUS_OK) return json_error("unsigned integer read failed", status);
    return value;
}

Expected<double> JsonValue::read_float64() const {
    double value = 0.0;
    const SntAbiStatus status = snt_json_value_read_float64(value_, &value);
    if (status != SNT_ABI_STATUS_OK) return json_error("floating-point read failed", status);
    return value;
}

Expected<std::string_view> JsonValue::read_string() const {
    SntAbiByteView value{nullptr, 0u};
    const SntAbiStatus status = snt_json_value_read_string(value_, &value);
    if (status != SNT_ABI_STATUS_OK) return json_error("string read failed", status);
    if (value.size_bytes == 0u) return std::string_view{};
    return std::string_view{
        reinterpret_cast<const char*>(value.data),
        static_cast<size_t>(value.size_bytes),
    };
}

Expected<std::optional<JsonValue>> JsonValue::object_find(std::string_view key) const {
    const SntJsonValue* found = nullptr;
    const SntAbiStatus status = snt_json_object_find(value_, byte_view(key), &found);
    if (status != SNT_ABI_STATUS_OK) return json_error("object lookup failed", status);
    if (found == nullptr) return std::optional<JsonValue>{};
    return std::optional<JsonValue>{JsonValue(found)};
}

Expected<uint64_t> JsonValue::array_count() const {
    uint64_t count = 0;
    const SntAbiStatus status = snt_json_array_count(value_, &count);
    if (status != SNT_ABI_STATUS_OK) return json_error("array count failed", status);
    return count;
}

Expected<JsonValue> JsonValue::array_get(uint64_t index) const {
    const SntJsonValue* element = nullptr;
    const SntAbiStatus status = snt_json_array_get(value_, index, &element);
    if (status != SNT_ABI_STATUS_OK) return json_error("array lookup failed", status);
    return JsonValue(element);
}

Expected<JsonDocument> JsonDocument::parse(std::string_view source) {
    SntJsonDocument* document = nullptr;
    const SntAbiStatus status = snt_json_document_parse(byte_view(source), &document);
    if (status != SNT_ABI_STATUS_OK) return json_error("parse failed", status);
    return JsonDocument(document);
}

JsonDocument::JsonDocument(JsonDocument&& other) noexcept
    : document_(std::exchange(other.document_, nullptr)) {}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept {
    if (this == &other) return *this;
    reset();
    document_ = std::exchange(other.document_, nullptr);
    return *this;
}

JsonDocument::~JsonDocument() {
    reset();
}

JsonValue JsonDocument::root() const noexcept {
    const SntJsonValue* value = nullptr;
    if (document_ == nullptr ||
        snt_json_document_root(document_, &value) != SNT_ABI_STATUS_OK) {
        return JsonValue{};
    }
    return JsonValue(value);
}

void JsonDocument::reset() noexcept {
    if (document_ == nullptr) return;
    snt_json_document_destroy(document_);
    document_ = nullptr;
}

}  // namespace snt::core
