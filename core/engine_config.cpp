// Engine config — JSON loader implementation.
//
// Uses nlohmann_json (already in the third-party tree via snt_third_party).
// Missing fields fall back to the struct defaults declared in the header,
// so a user-supplied JSON only needs to override what they want to change.

#define SNT_LOG_CHANNEL "config"
#include "core/log.h"
#include "core/engine_config.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace snt::core {

// ---------------------------------------------------------------------------
// JSON deserialization: each struct has a from_json overload so we can
// write `j.get<WindowConfig>()` etc. Missing fields keep their defaults
// (we construct a default instance first, then fill in whatever the JSON
// provides).
// ---------------------------------------------------------------------------
using json = nlohmann::json;

// Helper: read an optional field into `out` if present. Avoids throwing
// when the key is missing (nlohmann's j["key"].get<T>() throws on absence).
template<typename T>
void read_optional(const json& j, const char* key, T& out) {
    if (j.contains(key)) {
        out = j[key].get<T>();
    }
}

void from_json(const json& j, WindowConfig& w) {
    WindowConfig def;
    w = def;
    read_optional(j, "title",          w.title);
    read_optional(j, "width",          w.width);
    read_optional(j, "height",         w.height);
    read_optional(j, "fullscreen",     w.fullscreen);
    read_optional(j, "resizable",      w.resizable);
    read_optional(j, "vulkan_enabled", w.vulkan_enabled);
}

void from_json(const json& j, RenderConfig& r) {
    RenderConfig def;
    r = def;
    read_optional(j, "vert_shader_path",     r.vert_shader_path);
    read_optional(j, "frag_shader_path",     r.frag_shader_path);
    read_optional(j, "max_entities",         r.max_entities);
    read_optional(j, "max_frames_in_flight", r.max_frames_in_flight);
}

void from_json(const json& j, CameraConfig& c) {
    CameraConfig def;
    c = def;
    read_optional(j, "fov",        c.fov);
    read_optional(j, "near_plane", c.near_plane);
    read_optional(j, "far_plane",  c.far_plane);
    read_optional(j, "move_speed", c.move_speed);
    read_optional(j, "look_speed", c.look_speed);
    if (j.contains("initial_position") && j["initial_position"].is_array()) {
        const auto& arr = j["initial_position"];
        for (int i = 0; i < 3 && i < static_cast<int>(arr.size()); ++i) {
            c.initial_position[i] = arr[i].get<float>();
        }
    }
}

void from_json(const json& j, AssetConfig& a) {
    AssetConfig def;
    a = def;
    read_optional(j, "default_mesh_path", a.default_mesh_path);
    read_optional(j, "manifest_path",     a.manifest_path);
}

void from_json(const json& j, SceneConfig& s) {
    SceneConfig def;
    s = def;
    read_optional(j, "path", s.path);
}

void from_json(const json& j, EngineConfig& cfg) {
    cfg = EngineConfig{};
    if (j.contains("window")) cfg.window  = j["window"].get<WindowConfig>();
    if (j.contains("render")) cfg.render  = j["render"].get<RenderConfig>();
    if (j.contains("camera")) cfg.camera  = j["camera"].get<CameraConfig>();
    if (j.contains("assets")) cfg.assets  = j["assets"].get<AssetConfig>();
    if (j.contains("scene"))  cfg.scene   = j["scene"].get<SceneConfig>();
}

// ---------------------------------------------------------------------------
// load_engine_config
// ---------------------------------------------------------------------------
Expected<EngineConfig> load_engine_config(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        // Missing config is non-fatal: return defaults so the engine runs
        // out-of-the-box. Logged at INFO so the user knows what happened.
        SNT_LOG_INFO("Config file '%s' not found; using defaults", path.c_str());
        return EngineConfig{};
    }

    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string text = ss.str();

    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        return Error{ErrorCode::kInvalidArgument,
                     std::string("JSON parse error: ") + e.what()};
    }

    EngineConfig cfg = j.get<EngineConfig>();
    SNT_LOG_INFO("Config loaded from '%s'", path.c_str());
    return cfg;
}

}  // namespace snt::core
