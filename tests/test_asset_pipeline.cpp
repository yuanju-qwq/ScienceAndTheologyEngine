// Tests for P5 asset pipeline helpers: binary/JSON loading, stb_image texture
// loading, and material atlas assembly.

#include "assets/font_atlas.h"
#include "assets/loader.h"
#include "assets/material_atlas.h"
#include "assets/shader_cache.h"
#include "assets/texture_cache.h"
#include "core/path_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

}  // namespace

TEST(AssetPipelineTest, LoadBinaryFileReturnsExactBytes) {
    const auto path = temp_path("snt_asset_pipeline_bytes.bin");
    {
        std::ofstream out(path, std::ios::binary);
        const unsigned char bytes[] = {0x01, 0x7f, 0x80, 0xff};
        out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }

    auto result = snt::assets::load_binary_file(path.string());
    ASSERT_TRUE(result.has_value()) << result.error().format();
    ASSERT_EQ(result->size(), 4u);
    EXPECT_EQ((*result)[0], 0x01);
    EXPECT_EQ((*result)[1], 0x7f);
    EXPECT_EQ((*result)[2], 0x80);
    EXPECT_EQ((*result)[3], 0xff);
}

TEST(AssetPipelineTest, LoadJsonFileValidatesSyntax) {
    const auto path = temp_path("snt_asset_pipeline_valid.json");
    {
        std::ofstream out(path);
        out << R"({ "assets": [ { "id": "stone", "path": "stone.png" } ] })";
    }

    auto result = snt::assets::load_json_file(path.string());
    ASSERT_TRUE(result.has_value()) << result.error().format();
    EXPECT_NE(result->text.find("stone"), std::string::npos);
}

TEST(AssetPipelineTest, ShaderCacheLoadsSpirvWords) {
    const auto path = temp_path("snt_asset_pipeline_shader.spv");
    {
        std::ofstream out(path, std::ios::binary);
        const uint32_t words[] = {0x07230203u, 0x00010000u, 0x0008000au};
        out.write(reinterpret_cast<const char*>(words), sizeof(words));
    }

    snt::assets::ShaderCache cache;
    auto result = cache.load_spirv_binary(path.string());
    ASSERT_TRUE(result.has_value()) << result.error().format();
    ASSERT_EQ(result->size(), 3u);
    EXPECT_EQ((*result)[0], 0x07230203u);
    EXPECT_EQ((*result)[1], 0x00010000u);
    EXPECT_EQ((*result)[2], 0x0008000au);
}

TEST(AssetPipelineTest, ShaderCacheCompilesGlslWithShaderc) {
    const auto path = temp_path("snt_asset_pipeline_shader.vert");
    {
        std::ofstream out(path);
        out << R"(#version 450
layout(location = 0) in vec2 inPos;
void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
}
)";
    }

    snt::assets::ShaderCache cache;
    auto result = cache.compile_glsl_shaderc(
        path.string(), "main", snt::assets::ShaderStage::kVertex);
    ASSERT_TRUE(result.has_value()) << result.error().format();
    ASSERT_GE(result->size(), 5u);
    EXPECT_EQ((*result)[0], 0x07230203u);
}

TEST(AssetPipelineTest, FontAtlasBuildsWithFreeType) {
    const std::string font_path = "C:\\Windows\\Fonts\\arial.ttf";
    ASSERT_TRUE(std::filesystem::exists(font_path)) << font_path;

    snt::assets::FontAtlasBuildDesc desc;
    desc.font_path = font_path;
    desc.pixel_size = 16.0f;
    desc.atlas_width = 512;
    desc.atlas_height = 512;

    auto atlas = snt::assets::build_font_atlas_freetype(desc);
    ASSERT_TRUE(atlas.has_value()) << atlas.error().format();
    EXPECT_EQ(atlas->image.width, 512u);
    EXPECT_EQ(atlas->image.height, 512u);
    EXPECT_EQ(atlas->image.alpha.size(), 512u * 512u);
    EXPECT_GE(atlas->glyphs.size(), 95u);
    EXPECT_GT(atlas->line_height, 0.0f);
}

TEST(AssetPipelineTest, TextureCacheLoadsTerrainPng) {
    if (!snt::core::path_utils::init()) {
        GTEST_SKIP() << "path_utils could not locate project root";
    }
    const std::string path =
        snt::core::path_utils::resolve("resource/terrain/stone/stone_tile_32.png");
    if (!snt::core::path_utils::exists(path)) {
        GTEST_SKIP() << "terrain texture missing: " << path;
    }

    snt::assets::TextureCache cache;
    auto image = cache.load_rgba(path);
    ASSERT_TRUE(image.has_value()) << image.error().format();
    EXPECT_EQ((*image)->width, 32);
    EXPECT_EQ((*image)->height, 32);
    EXPECT_EQ((*image)->rgba.size(), 32u * 32u * 4u);
}

TEST(AssetPipelineTest, MaterialAtlasFailsWhenTextureIsMissing) {
    snt::assets::TextureCache cache;
    auto atlas = snt::assets::build_material_atlas(
        cache,
        {"missing_tile_a.png", "missing_tile_b.png"},
        8);

    ASSERT_FALSE(atlas.has_value());
    EXPECT_NE(atlas.error().format().find("build_material_atlas tile 0"),
              std::string::npos);
}
