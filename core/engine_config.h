// Engine configuration: single source of truth for all tunable parameters.
//
// Design goals:
//   - Replace every hardcoded constant (window size, shader paths, camera
//     defaults, asset paths, render limits) with a field in EngineConfig.
//   - Load from a game-owned JSON file at startup; fall back to sensible
//     defaults if the file is missing so the engine always runs.
//   - Subsystem constructors receive only the slice they need (WindowConfig
//     / RenderConfig / CameraConfig / AssetConfig), keeping module
//     boundaries clean.
//   - Future: hot-reload triggers a ConfigReloaded event on the EventBus so
//     subsystems can re-read their slice without a restart.
//
// Layering note: lives in core/ because every layer above reads from it.
// The JSON loader uses nlohmann_json (linked via snt_third_party); the
// header itself is dependency-free so it can be included from anywhere.

#pragma once

#include "core/expected.h"  // Expected<T> for load_engine_config

#include <cstdint>
#include <string>
#include <vector>

namespace snt::core {

// ---------------------------------------------------------------------------
// Window configuration
// ---------------------------------------------------------------------------
struct WindowConfig {
    std::string title = "ScienceAndTheology Engine";
    int  width  = 1280;
    int  height = 720;
    bool fullscreen   = false;
    bool resizable    = true;
    bool vulkan_enabled = true;
};

// ---------------------------------------------------------------------------
// Render configuration
// ---------------------------------------------------------------------------
// `max_entities` controls the dynamic UBO slot count (one MVP per entity).
// Currently mirrors VulkanDescriptor's runtime limit; raising it increases
// UBO allocation size linearly.
struct RenderConfig {
    std::string vert_shader_path = "shaders/mesh.vert.spv";
    std::string frag_shader_path = "shaders/mesh.frag.spv";
    uint32_t    max_entities        = 256;
    uint32_t    max_frames_in_flight = 2;
};

// ---------------------------------------------------------------------------
// Voxel rendering configuration
// ---------------------------------------------------------------------------
// `max_chunks` controls the dynamic UBO slot count for chunk draw calls.
// The per-frame budgets keep remesh/upload work bounded so terrain changes
// do not stall a logic tick.
struct VoxelConfig {
    uint32_t max_chunks = 1024;
    uint32_t remesh_jobs_per_frame = 4;
    uint32_t uploads_per_frame = 2;
};

// ---------------------------------------------------------------------------
// UI configuration
// ---------------------------------------------------------------------------
// Retained MUI always uses the Unicode text backend. The ordered family is
// shaped and rasterized as one modern text pipeline; no legacy text path is
// available when a font entry cannot be used.
struct UiConfig {
    std::vector<std::string> font_paths{
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/seguiemj.ttf",
    };
    std::string locale = "zh-Hans";
    std::string icu_data_path = "third_party/icu4c/icudt_godot.dat";
};

// ---------------------------------------------------------------------------
// Camera configuration
// ---------------------------------------------------------------------------
// `initial_position` is the spawn pose of the active camera entity.
// `move_speed` / `look_speed` map directly to CameraSystem fields.
struct CameraConfig {
    float fov        = 60.0f;
    float near_plane = 0.1f;
    float far_plane  = 100.0f;
    float move_speed = 3.0f;   // units per second
    float look_speed = 0.1f;   // degrees per pixel
    float initial_position[3] = {0.0f, 0.0f, 3.0f};
};

// ---------------------------------------------------------------------------
// Asset configuration
// ---------------------------------------------------------------------------
// `default_mesh_path` is the mesh loaded at startup for the demo cube
// entities. P3 will replace this with a proper asset manifest.
// `manifest_path` points at the asset manifest JSON (declares stable
// handle<->path mappings; see assets/asset_manifest.h). Empty string
// disables manifest-based pre-allocation (falls back to runtime load()).
struct AssetConfig {
    std::string default_mesh_path = "assets/dev/cube.obj";
    std::string manifest_path     = "config/default_manifest.json";
};

// ---------------------------------------------------------------------------
// Scene configuration
// ---------------------------------------------------------------------------
// `path` is the binary scene file to load at startup. If the file is
// missing, Engine::init falls back to the hardcoded demo scene (two
// cubes + a camera) so the engine always runs out-of-the-box. The
// scene format is documented in scene/scene.h.
struct SceneConfig {
    std::string path = "scenes/default_scene.bin";
};

// ---------------------------------------------------------------------------
// Gameplay script configuration
// ---------------------------------------------------------------------------
// P7.1 watches this root from the main thread. Missing roots are allowed so
// a content-free engine build remains runnable; the engine logs that no
// gameplay modules were loaded.
struct ScriptConfig {
    bool enabled = true;
    bool watch_for_changes = true;
    std::string root = "scripts";
};

// ---------------------------------------------------------------------------
// Development/demo configuration
// ---------------------------------------------------------------------------
// Keeps verification content out of the core engine path. Production builds
// can disable this and load terrain through the real world/session pipeline.
struct DemoConfig {
    bool bootstrap_chunks = true;
    uint32_t seed = 20240601u;
};

// ---------------------------------------------------------------------------
// Top-level config aggregate
// ---------------------------------------------------------------------------
struct EngineConfig {
    WindowConfig  window;
    RenderConfig  render;
    VoxelConfig   voxel;
    UiConfig      ui;
    CameraConfig  camera;
    AssetConfig   assets;
    SceneConfig   scene;
    ScriptConfig  scripts;
    DemoConfig    demo;
};

// Load EngineConfig from a JSON file.
//
// Behavior:
//   - File missing: returns a default-constructed EngineConfig (no error).
//     This lets the engine run out-of-the-box without a config file.
//   - JSON parse error: returns an Error describing the parse failure.
//   - Unknown fields: silently ignored (forward-compatible).
//   - Missing fields: fall back to the defaults from the struct above.
//
// Example game-owned JSON:
//   {
//     "window":  { "width": 1920, "height": 1080, "title": "SNT" },
//     "render":  { "vert_shader_path": "shaders/mesh.vert.spv" },
//     "voxel":   { "max_chunks": 1024 },
//     "ui":      { "font_paths": ["resource/fonts/NotoSans-Regular.ttf"] },
//     "camera":  { "fov": 75.0, "move_speed": 5.0 },
//     "assets":  { "default_mesh_path": "assets/dev/cube.obj" }
//   }
Expected<EngineConfig> load_engine_config(const std::string& path);

}  // namespace snt::core
