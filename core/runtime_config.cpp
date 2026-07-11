// Runtime configuration JSON loader.

#define SNT_LOG_CHANNEL "runtime_config"
#include "core/log.h"
#include "core/runtime_config.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace snt::core {
using json = nlohmann::json;

namespace {

template <typename T>
void read_optional(const json& object, const char* key, T& out) {
    if (object.contains(key)) {
        out = object[key].get<T>();
    }
}

}  // namespace

void from_json(const json& object, WindowConfig& value) {
    value = WindowConfig{};
    read_optional(object, "title", value.title);
    read_optional(object, "width", value.width);
    read_optional(object, "height", value.height);
    read_optional(object, "fullscreen", value.fullscreen);
    read_optional(object, "resizable", value.resizable);
    read_optional(object, "vulkan_enabled", value.vulkan_enabled);
}

void from_json(const json& object, RenderConfig& value) {
    value = RenderConfig{};
    read_optional(object, "vert_shader_path", value.vert_shader_path);
    read_optional(object, "frag_shader_path", value.frag_shader_path);
    read_optional(object, "max_entities", value.max_entities);
    read_optional(object, "max_frames_in_flight", value.max_frames_in_flight);
}

void from_json(const json& object, VoxelConfig& value) {
    value = VoxelConfig{};
    read_optional(object, "max_chunks", value.max_chunks);
    read_optional(object, "remesh_jobs_per_frame", value.remesh_jobs_per_frame);
    read_optional(object, "uploads_per_frame", value.uploads_per_frame);
}

void from_json(const json& object, UiConfig& value) {
    value = UiConfig{};
    read_optional(object, "font_paths", value.font_paths);
    read_optional(object, "locale", value.locale);
    read_optional(object, "icu_data_path", value.icu_data_path);
}

void from_json(const json& object, RuntimeConfig& value) {
    value = RuntimeConfig{};
    if (object.contains("window")) value.window = object["window"].get<WindowConfig>();
    if (object.contains("render")) value.render = object["render"].get<RenderConfig>();
    if (object.contains("voxel")) value.voxel = object["voxel"].get<VoxelConfig>();
    if (object.contains("ui")) value.ui = object["ui"].get<UiConfig>();
}

Expected<RuntimeConfig> load_runtime_config(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        SNT_LOG_INFO("Runtime config '%s' not found; using defaults", path.c_str());
        return RuntimeConfig{};
    }

    std::stringstream buffer;
    buffer << input.rdbuf();

    try {
        const json object = json::parse(buffer.str());
        SNT_LOG_INFO("Runtime config loaded from '%s'", path.c_str());
        return object.get<RuntimeConfig>();
    } catch (const std::exception& error) {
        return Error{ErrorCode::kInvalidArgument,
                     std::string("Runtime config JSON parse error: ") + error.what()};
    }
}

}  // namespace snt::core
