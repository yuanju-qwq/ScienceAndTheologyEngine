// Project-owned C++ facade for immutable JSON documents.
//
// C++ callers use these value types instead of depending on nlohmann_json or
// Zig std.json. The implementation delegates to json_abi.h, whose Zig-owned
// document storage remains valid for the lifetime of JsonDocument.

#pragma once

#include "core/expected.h"

#include <cstdint>
#include <optional>
#include <string_view>

struct SntJsonDocument;
struct SntJsonValue;

namespace snt::core {

enum class JsonValueType : uint32_t {
    kInvalid = 0,
    kNull = 1,
    kBool = 2,
    kInteger = 3,
    kFloat = 4,
    kNumberString = 5,
    kString = 6,
    kArray = 7,
    kObject = 8,
};

class JsonValue {
public:
    JsonValue() = default;

    [[nodiscard]] bool valid() const noexcept { return value_ != nullptr; }
    [[nodiscard]] Expected<JsonValueType> type() const;
    [[nodiscard]] Expected<bool> read_bool() const;
    [[nodiscard]] Expected<int64_t> read_int64() const;
    [[nodiscard]] Expected<uint64_t> read_uint64() const;
    [[nodiscard]] Expected<double> read_float64() const;
    [[nodiscard]] Expected<std::string_view> read_string() const;

    // A missing field returns an engaged Expected containing std::nullopt.
    [[nodiscard]] Expected<std::optional<JsonValue>> object_find(
        std::string_view key) const;
    [[nodiscard]] Expected<uint64_t> array_count() const;
    [[nodiscard]] Expected<JsonValue> array_get(uint64_t index) const;

private:
    friend class JsonDocument;
    explicit JsonValue(const ::SntJsonValue* value) noexcept : value_(value) {}

    const ::SntJsonValue* value_ = nullptr;
};

class JsonDocument {
public:
    [[nodiscard]] static Expected<JsonDocument> parse(std::string_view source);

    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(JsonDocument&& other) noexcept;
    ~JsonDocument();

    [[nodiscard]] bool valid() const noexcept { return document_ != nullptr; }
    [[nodiscard]] JsonValue root() const noexcept;

private:
    explicit JsonDocument(::SntJsonDocument* document) noexcept : document_(document) {}
    void reset() noexcept;

    ::SntJsonDocument* document_ = nullptr;
};

}  // namespace snt::core
