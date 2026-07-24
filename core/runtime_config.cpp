// Runtime configuration JSON loader.

#define SNT_LOG_CHANNEL "runtime_config"
#include "core/log.h"
#include "core/json.h"
#include "core/runtime_config.h"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace snt::core {

namespace {

Error config_error(std::string_view path, std::string_view detail) {
    std::string message = "Runtime config JSON parse error";
    if (!path.empty()) {
        message += " at ";
        message += path;
    }
    message += ": ";
    message += detail;
    return Error{ErrorCode::kInvalidArgument, message};
}

std::string indexed_path(std::string_view path, size_t index) {
    return std::string(path) + "[" + std::to_string(index) + "]";
}

Expected<std::optional<JsonValue>> find_optional(
    const JsonValue& object,
    std::string_view key,
    std::string_view path) {
    auto field = object.object_find(key);
    if (!field) return config_error(path, field.error().message());
    return *field;
}

Expected<void> require_object(const JsonValue& value, std::string_view path) {
    auto type = value.type();
    if (!type) return config_error(path, type.error().message());
    if (*type != JsonValueType::kObject) {
        return config_error(path, "expected an object");
    }
    return {};
}

Expected<void> read_optional_string(
    const JsonValue& object,
    std::string_view key,
    std::string_view path,
    std::string& out) {
    auto field = find_optional(object, key, path);
    if (!field) return field.error();
    if (!field->has_value()) return {};

    auto text = field->value().read_string();
    if (!text) return config_error(path, text.error().message());
    out = *text;
    return {};
}

Expected<void> read_optional_bool(
    const JsonValue& object,
    std::string_view key,
    std::string_view path,
    bool& out) {
    auto field = find_optional(object, key, path);
    if (!field) return field.error();
    if (!field->has_value()) return {};

    auto boolean = field->value().read_bool();
    if (!boolean) return config_error(path, boolean.error().message());
    out = *boolean;
    return {};
}

Expected<void> read_optional_int(
    const JsonValue& object,
    std::string_view key,
    std::string_view path,
    int& out) {
    auto field = find_optional(object, key, path);
    if (!field) return field.error();
    if (!field->has_value()) return {};

    auto number = field->value().read_int64();
    if (!number) return config_error(path, number.error().message());
    if (*number < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
        *number > static_cast<int64_t>(std::numeric_limits<int>::max())) {
        return config_error(path, "integer is outside the supported range");
    }
    out = static_cast<int>(*number);
    return {};
}

Expected<void> read_optional_uint32(
    const JsonValue& object,
    std::string_view key,
    std::string_view path,
    uint32_t& out) {
    auto field = find_optional(object, key, path);
    if (!field) return field.error();
    if (!field->has_value()) return {};

    auto number = field->value().read_uint64();
    if (!number) return config_error(path, number.error().message());
    if (*number > std::numeric_limits<uint32_t>::max()) {
        return config_error(path, "integer is outside the supported range");
    }
    out = static_cast<uint32_t>(*number);
    return {};
}

Expected<void> read_optional_float(
    const JsonValue& object,
    std::string_view key,
    std::string_view path,
    float& out) {
    auto field = find_optional(object, key, path);
    if (!field) return field.error();
    if (!field->has_value()) return {};

    auto number = field->value().read_float64();
    if (!number) return config_error(path, number.error().message());
    if (!std::isfinite(*number) ||
        *number < static_cast<double>(std::numeric_limits<float>::lowest()) ||
        *number > static_cast<double>(std::numeric_limits<float>::max())) {
        return config_error(path, "number is outside the supported range");
    }
    out = static_cast<float>(*number);
    return {};
}

Expected<void> read_optional_string_array(
    const JsonValue& object,
    std::string_view key,
    std::string_view path,
    std::vector<std::string>& out) {
    auto field = find_optional(object, key, path);
    if (!field) return field.error();
    if (!field->has_value()) return {};

    auto count = field->value().array_count();
    if (!count) return config_error(path, count.error().message());
    if (*count > std::numeric_limits<size_t>::max()) {
        return config_error(path, "array is too large");
    }

    const size_t size = static_cast<size_t>(*count);
    std::vector<std::string> values;
    if (size > values.max_size()) return config_error(path, "array is too large");
    values.reserve(size);
    for (size_t index = 0; index < size; ++index) {
        const std::string item_path = indexed_path(path, index);
        auto element = field->value().array_get(index);
        if (!element) return config_error(item_path, element.error().message());
        auto text = element->read_string();
        if (!text) return config_error(item_path, text.error().message());
        values.emplace_back(*text);
    }

    out = std::move(values);
    return {};
}

Expected<void> load_window_config(const JsonValue& root, RuntimeConfig& config) {
    auto section = find_optional(root, "window", "window");
    if (!section) return section.error();
    if (!section->has_value()) return {};

    const JsonValue object = section->value();
    if (auto result = require_object(object, "window"); !result) return result.error();
    if (auto result = read_optional_string(object, "title", "window.title", config.window.title);
        !result) return result.error();
    if (auto result = read_optional_int(object, "width", "window.width", config.window.width);
        !result) return result.error();
    if (auto result = read_optional_int(object, "height", "window.height", config.window.height);
        !result) return result.error();
    if (auto result = read_optional_bool(object, "fullscreen", "window.fullscreen",
                                         config.window.fullscreen);
        !result) return result.error();
    if (auto result = read_optional_bool(object, "resizable", "window.resizable",
                                         config.window.resizable);
        !result) return result.error();
    return read_optional_bool(object, "vulkan_enabled", "window.vulkan_enabled",
                              config.window.vulkan_enabled);
}

Expected<void> load_render_config(const JsonValue& root, RuntimeConfig& config) {
    auto section = find_optional(root, "render", "render");
    if (!section) return section.error();
    if (!section->has_value()) return {};

    const JsonValue object = section->value();
    if (auto result = require_object(object, "render"); !result) return result.error();
    if (auto result = read_optional_string(object, "vert_shader_path", "render.vert_shader_path",
                                            config.render.vert_shader_path);
        !result) return result.error();
    if (auto result = read_optional_string(object, "frag_shader_path", "render.frag_shader_path",
                                            config.render.frag_shader_path);
        !result) return result.error();
    if (auto result = read_optional_uint32(object, "max_entities", "render.max_entities",
                                            config.render.max_entities);
        !result) return result.error();
    return read_optional_uint32(object, "max_frames_in_flight", "render.max_frames_in_flight",
                                config.render.max_frames_in_flight);
}

Expected<void> load_asset_config(const JsonValue& root, RuntimeConfig& config) {
    auto section = find_optional(root, "assets", "assets");
    if (!section) return section.error();
    if (!section->has_value()) return {};

    const JsonValue object = section->value();
    if (auto result = require_object(object, "assets"); !result) return result.error();
    return read_optional_string(object, "manifest_path", "assets.manifest_path",
                                config.assets.manifest_path);
}

Expected<void> load_voxel_config(const JsonValue& root, RuntimeConfig& config) {
    auto section = find_optional(root, "voxel", "voxel");
    if (!section) return section.error();
    if (!section->has_value()) return {};

    const JsonValue object = section->value();
    if (auto result = require_object(object, "voxel"); !result) return result.error();
    if (auto result = read_optional_uint32(object, "max_chunks", "voxel.max_chunks",
                                            config.voxel.max_chunks);
        !result) return result.error();
    if (auto result = read_optional_uint32(object, "remesh_jobs_per_frame",
                                            "voxel.remesh_jobs_per_frame",
                                            config.voxel.remesh_jobs_per_frame);
        !result) return result.error();
    return read_optional_uint32(object, "uploads_per_frame", "voxel.uploads_per_frame",
                                config.voxel.uploads_per_frame);
}

Expected<void> load_ui_config(const JsonValue& root, RuntimeConfig& config) {
    auto section = find_optional(root, "ui", "ui");
    if (!section) return section.error();
    if (!section->has_value()) return {};

    const JsonValue object = section->value();
    if (auto result = require_object(object, "ui"); !result) return result.error();
    if (auto result = read_optional_float(object, "scale", "ui.scale", config.ui.scale);
        !result) return result.error();
    if (auto result = read_optional_string_array(object, "font_paths", "ui.font_paths",
                                                 config.ui.font_paths);
        !result) return result.error();
    if (auto result = read_optional_string(object, "locale", "ui.locale", config.ui.locale);
        !result) return result.error();
    if (auto result = read_optional_string(object, "icu_data_path", "ui.icu_data_path",
                                            config.ui.icu_data_path);
        !result) return result.error();

    if (!std::isfinite(config.ui.scale) || config.ui.scale <= 0.0f) {
        config.ui.scale = UiConfig{}.scale;
        SNT_LOG_WARN("Invalid ui.scale in runtime config; using %.2f", config.ui.scale);
    }
    return {};
}

}  // namespace

Expected<RuntimeConfig> load_runtime_config(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        SNT_LOG_INFO("Runtime config '%s' not found; using defaults", path.c_str());
        return RuntimeConfig{};
    }

    std::stringstream buffer;
    buffer << input.rdbuf();

    const std::string source = buffer.str();
    auto document = JsonDocument::parse(source);
    const auto reject = [&path](const Error& error) -> Expected<RuntimeConfig> {
        SNT_LOG_WARN("Runtime config '%s' rejected: %s", path.c_str(), error.message().c_str());
        return error;
    };
    if (!document) return reject(config_error({}, document.error().message()));

    const JsonValue root = document->root();
    if (auto result = require_object(root, "root"); !result) return reject(result.error());

    RuntimeConfig config{};
    if (auto result = load_window_config(root, config); !result) return reject(result.error());
    if (auto result = load_render_config(root, config); !result) return reject(result.error());
    if (auto result = load_asset_config(root, config); !result) return reject(result.error());
    if (auto result = load_voxel_config(root, config); !result) return reject(result.error());
    if (auto result = load_ui_config(root, config); !result) return reject(result.error());

    SNT_LOG_INFO("Runtime config loaded from '%s'", path.c_str());
    return config;
}

}  // namespace snt::core
