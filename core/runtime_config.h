// Runtime configuration owned by the engine runtime.
//
// This module intentionally contains only generic runtime settings needed
// before an IGameSession is created. Scene, script, player, and demo world
// settings belong to the game-owned session configuration; the generic asset
// manifest bootstrap remains here because Runtime must publish its catalog
// before the session creates a World.

#pragma once

#include "core/expected.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snt::core {

struct WindowConfig {
    std::string title = "ScienceAndTheology Runtime";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool resizable = true;
    bool vulkan_enabled = true;
};

struct RenderConfig {
    std::string vert_shader_path = "shaders/mesh.vert.spv";
    std::string frag_shader_path = "shaders/mesh.frag.spv";
    uint32_t max_entities = 256;
    uint32_t max_frames_in_flight = 2;
};

// Generic content bootstrap settings needed before IGameSession creates a
// World. The manifest belongs to the game package, while Runtime owns the
// source/catalog lifetime and makes the immutable catalog available to the
// session through RuntimeServices.
struct AssetConfig {
    std::string manifest_path = "config/default_manifest.json";
};

struct VoxelConfig {
    uint32_t max_chunks = 1024;
    uint32_t remesh_jobs_per_frame = 4;
    uint32_t uploads_per_frame = 2;
};

struct UiConfig {
    std::vector<std::string> font_paths{
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/seguiemj.ttf",
    };
    std::string locale = "zh-Hans";
    std::string icu_data_path = "third_party/icu4c/icudt_godot.dat";
};

struct RuntimeConfig {
    WindowConfig window;
    RenderConfig render;
    AssetConfig assets;
    VoxelConfig voxel;
    UiConfig ui;
};

// Loads the runtime-owned subset from a game package JSON file. Unknown keys
// are ignored so the game can store its session configuration in the same file.
Expected<RuntimeConfig> load_runtime_config(const std::string& path);

}  // namespace snt::core
