#pragma once

#include "core/expected.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snt::assets {

struct FontAtlasBuildDesc {
    std::string font_path;
    float pixel_size = 16.0f;
    uint32_t atlas_width = 512;
    uint32_t atlas_height = 512;
};

struct FontAtlasImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> alpha;
};

struct FontGlyph {
    uint32_t codepoint = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t bearing_x = 0;
    int32_t bearing_y = 0;
    int32_t advance_x = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

struct FontAtlas {
    FontAtlasImage image;
    std::vector<FontGlyph> glyphs;
    float line_height = 0.0f;
};

snt::core::Expected<FontAtlas> build_font_atlas_freetype(
    const FontAtlasBuildDesc& desc);

}  // namespace snt::assets
