// Tests for EngineConfig JSON loader.
//
// Covers:
//   - Default values when a field is absent
//   - Override of specific fields
//   - Parse error returns an Error, not a crash
//   - Missing file returns defaults (non-fatal)

#include "core/engine_config.h"

#include <gtest/gtest.h>

#include <fstream>
#include <filesystem>

using snt::core::EngineConfig;
using snt::core::load_engine_config;

namespace {

// Helper: write a temporary JSON file with the given contents and return
// its path. The file is created in the system temp dir.
std::string write_temp_json(const std::string& name, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream ofs(path);
    ofs << contents;
    ofs.close();
    return path.string();
}

}  // namespace

TEST(EngineConfig, MissingFileReturnsDefaults) {
    auto result = load_engine_config("definitely_does_not_exist.json");
    ASSERT_TRUE(result.has_value());
    const auto& cfg = *result;
    EXPECT_EQ(cfg.window.width, 1280);
    EXPECT_EQ(cfg.window.height, 720);
    EXPECT_EQ(cfg.render.max_entities, 256u);
    EXPECT_FLOAT_EQ(cfg.camera.fov, 60.0f);
    EXPECT_EQ(cfg.assets.default_mesh_path, "assets/cube.obj");
}

TEST(EngineConfig, OverridesApplied) {
    const std::string json = R"({
        "window": { "width": 1920, "height": 1080, "title": "Test" },
        "render": { "max_entities": 512 },
        "camera": { "fov": 90.0, "move_speed": 5.5 },
        "assets": { "default_mesh_path": "assets/test.obj" }
    })";
    const auto path = write_temp_json("snt_test_overrides.json", json);

    auto result = load_engine_config(path);
    ASSERT_TRUE(result.has_value());
    const auto& cfg = *result;
    EXPECT_EQ(cfg.window.width, 1920);
    EXPECT_EQ(cfg.window.height, 1080);
    EXPECT_EQ(cfg.window.title, "Test");
    // Untouched fields keep defaults.
    EXPECT_TRUE(cfg.window.resizable);
    EXPECT_EQ(cfg.render.max_entities, 512u);
    EXPECT_EQ(cfg.render.vert_shader_path, "shaders/mesh.vert.spv");
    EXPECT_FLOAT_EQ(cfg.camera.fov, 90.0f);
    EXPECT_FLOAT_EQ(cfg.camera.move_speed, 5.5f);
    EXPECT_FLOAT_EQ(cfg.camera.near_plane, 0.1f);  // default kept
    EXPECT_EQ(cfg.assets.default_mesh_path, "assets/test.obj");
}

TEST(EngineConfig, ParseErrorReturnsError) {
    const std::string bad_json = "{ this is not valid json ";
    const auto path = write_temp_json("snt_test_bad.json", bad_json);

    auto result = load_engine_config(path);
    ASSERT_FALSE(result.has_value());
    // Error should mention a parse failure.
    EXPECT_NE(result.error().message().find("parse"), std::string::npos);
}

TEST(EngineConfig, PartialOverrideKeepsOtherDefaults) {
    const std::string json = R"({ "camera": { "fov": 75.0 } })";
    const auto path = write_temp_json("snt_test_partial.json", json);

    auto result = load_engine_config(path);
    ASSERT_TRUE(result.has_value());
    const auto& cfg = *result;
    EXPECT_FLOAT_EQ(cfg.camera.fov, 75.0f);
    // Everything else stays default.
    EXPECT_EQ(cfg.window.width, 1280);
    EXPECT_EQ(cfg.render.max_entities, 256u);
}

TEST(EngineConfig, InitialPositionArrayParsed) {
    const std::string json = R"({
        "camera": { "initial_position": [1.0, 2.0, 3.0] }
    })";
    const auto path = write_temp_json("snt_test_pos.json", json);

    auto result = load_engine_config(path);
    ASSERT_TRUE(result.has_value());
    const auto& cfg = *result;
    EXPECT_FLOAT_EQ(cfg.camera.initial_position[0], 1.0f);
    EXPECT_FLOAT_EQ(cfg.camera.initial_position[1], 2.0f);
    EXPECT_FLOAT_EQ(cfg.camera.initial_position[2], 3.0f);
}
