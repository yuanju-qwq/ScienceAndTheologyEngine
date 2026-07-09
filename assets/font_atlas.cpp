#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/font_atlas.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdint>
#include <string>

namespace snt::assets {

snt::core::Expected<FontAtlas> build_font_atlas_freetype(
        const FontAtlasBuildDesc& desc) {
    if (desc.font_path.empty() || desc.pixel_size <= 0.0f ||
        desc.atlas_width == 0 || desc.atlas_height == 0) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "build_font_atlas_freetype: invalid font atlas desc"};
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                "FT_Init_FreeType failed"};
    }

    FT_Face face = nullptr;
    if (FT_New_Face(library, desc.font_path.c_str(), 0, &face) != 0) {
        FT_Done_FreeType(library);
        return snt::core::Error{snt::core::ErrorCode::kFileOpenFailed,
                                "FT_New_Face failed: " + desc.font_path};
    }

    const auto pixel_size = static_cast<FT_UInt>(
        std::max(1.0f, desc.pixel_size));
    if (FT_Set_Pixel_Sizes(face, 0, pixel_size) != 0) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                "FT_Set_Pixel_Sizes failed"};
    }

    FontAtlas atlas;
    atlas.image.width = desc.atlas_width;
    atlas.image.height = desc.atlas_height;
    atlas.image.alpha.assign(
        static_cast<size_t>(desc.atlas_width) * desc.atlas_height, 0);
    atlas.glyphs.reserve(95);
    atlas.line_height = static_cast<float>(face->size->metrics.height >> 6);
    if (atlas.line_height <= 0.0f) {
        atlas.line_height = desc.pixel_size * 1.25f;
    }

    uint32_t pen_x = 1;
    uint32_t pen_y = 1;
    uint32_t row_height = 0;

    for (uint32_t codepoint = 32; codepoint <= 126; ++codepoint) {
        if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                    "FT_Load_Char failed for codepoint " +
                                        std::to_string(codepoint)};
        }

        const FT_GlyphSlot glyph = face->glyph;
        const FT_Bitmap& bitmap = glyph->bitmap;
        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.width > 0 && bitmap.rows > 0) {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                    "FreeType glyph bitmap is not 8-bit grayscale"};
        }

        if (bitmap.width + 2 > desc.atlas_width ||
            bitmap.rows + 2 > desc.atlas_height) {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "Font glyph is larger than the atlas"};
        }

        if (pen_x + bitmap.width + 1 > desc.atlas_width) {
            pen_x = 1;
            pen_y += row_height + 1;
            row_height = 0;
        }
        if (pen_y + bitmap.rows + 1 > desc.atlas_height) {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return snt::core::Error{snt::core::ErrorCode::kAssetLoadFailed,
                                    "Font atlas is too small for ASCII glyphs"};
        }

        if (bitmap.width > 0 && bitmap.rows > 0 && bitmap.buffer) {
            const int pitch = bitmap.pitch;
            const int abs_pitch = pitch < 0 ? -pitch : pitch;
            for (uint32_t y = 0; y < bitmap.rows; ++y) {
                const uint32_t src_y = pitch >= 0 ? y : (bitmap.rows - 1 - y);
                const uint8_t* src_row = bitmap.buffer + src_y * abs_pitch;
                uint8_t* dst_row = atlas.image.alpha.data() +
                    (static_cast<size_t>(pen_y + y) * desc.atlas_width + pen_x);
                std::copy(src_row, src_row + bitmap.width, dst_row);
            }
        }

        FontGlyph out;
        out.codepoint = codepoint;
        out.x = pen_x;
        out.y = pen_y;
        out.width = bitmap.width;
        out.height = bitmap.rows;
        out.bearing_x = glyph->bitmap_left;
        out.bearing_y = glyph->bitmap_top;
        out.advance_x = static_cast<int32_t>(glyph->advance.x >> 6);
        out.u0 = static_cast<float>(pen_x) / static_cast<float>(desc.atlas_width);
        out.v0 = static_cast<float>(pen_y) / static_cast<float>(desc.atlas_height);
        out.u1 = static_cast<float>(pen_x + bitmap.width) / static_cast<float>(desc.atlas_width);
        out.v1 = static_cast<float>(pen_y + bitmap.rows) / static_cast<float>(desc.atlas_height);
        atlas.glyphs.push_back(out);

        pen_x += std::max<uint32_t>(1, bitmap.width) + 1;
        row_height = std::max<uint32_t>(row_height, bitmap.rows);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    SNT_LOG_INFO("FreeType font atlas built: %s (%ux%u, glyphs=%zu)",
                 desc.font_path.c_str(), atlas.image.width, atlas.image.height,
                 atlas.glyphs.size());
    return atlas;
}

}  // namespace snt::assets
