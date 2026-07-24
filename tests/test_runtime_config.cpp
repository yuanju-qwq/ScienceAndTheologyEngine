// Tests for RuntimeConfig JSON loading.

#include "core/runtime_config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using snt::core::ErrorCode;
using snt::core::RuntimeConfig;
using snt::core::load_runtime_config;

namespace {

std::string write_temp_json(const std::string& name, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path);
    output << contents;
    return path.string();
}

}  // namespace

TEST(RuntimeConfig, MissingFileReturnsDefaults) {
    auto result = load_runtime_config("definitely_does_not_exist.json");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->window.width, 1280);
    EXPECT_EQ(result->window.height, 720);
    EXPECT_EQ(result->render.max_entities, 256u);
    EXPECT_EQ(result->assets.manifest_path, "config/default_manifest.json");
    EXPECT_EQ(result->voxel.max_chunks, 1024u);
}

TEST(RuntimeConfig, OverridesRuntimeFieldsAndIgnoresGameFields) {
    const auto path = write_temp_json("snt_runtime_config_overrides.json", R"({
        "window": { "width": 1920, "height": 1080, "title": "Test" },
        "render": { "max_entities": 512 },
        "assets": { "manifest_path": "content/main_assets.json" },
        "voxel": { "max_chunks": 2048 },
        "scene": { "path": "scenes/game.bin" },
        "scripts": { "enabled": false }
    })");

    auto result = load_runtime_config(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->window.width, 1920);
    EXPECT_EQ(result->window.height, 1080);
    EXPECT_EQ(result->window.title, "Test");
    EXPECT_EQ(result->render.max_entities, 512u);
    EXPECT_EQ(result->assets.manifest_path, "content/main_assets.json");
    EXPECT_EQ(result->voxel.max_chunks, 2048u);
    EXPECT_TRUE(result->window.resizable);
}

TEST(RuntimeConfig, LoadsUiSettingsAndFontPathsThroughJsonFacade) {
    const auto path = write_temp_json("snt_runtime_config_ui.json", R"({
        "ui": {
            "scale": 1.25,
            "font_paths": ["fonts/primary.ttf", "fonts/fallback.ttc"],
            "locale": "en-US",
            "icu_data_path": "content/icu.dat"
        }
    })");

    auto result = load_runtime_config(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result->ui.scale, 1.25f);
    ASSERT_EQ(result->ui.font_paths.size(), 2u);
    EXPECT_EQ(result->ui.font_paths[0], "fonts/primary.ttf");
    EXPECT_EQ(result->ui.font_paths[1], "fonts/fallback.ttc");
    EXPECT_EQ(result->ui.locale, "en-US");
    EXPECT_EQ(result->ui.icu_data_path, "content/icu.dat");
}

TEST(RuntimeConfig, RejectsInvalidFieldTypesAndRanges) {
    const auto path = write_temp_json("snt_runtime_config_invalid_field.json", R"({
        "render": { "max_entities": -1 }
    })");

    const auto result = load_runtime_config(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::kInvalidArgument);
    EXPECT_NE(result.error().message().find("render.max_entities"), std::string::npos);
}

TEST(RuntimeConfig, ParseErrorReturnsError) {
    const auto path = write_temp_json("snt_runtime_config_bad.json", "{ invalid json");
    const auto result = load_runtime_config(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::kInvalidArgument);
    EXPECT_NE(result.error().message().find("parse"), std::string::npos);
}
