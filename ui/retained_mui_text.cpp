// Retained-MUI Unicode text shaping and glyph atlas.
//
// This module owns ICU initialization, HarfBuzz shaping, FreeType glyph
// rasterization, and atlas updates. Views consume only the TextEngine API.

#define SNT_LOG_CHANNEL "ui.text"
#include "retained_mui_text.h"
#include "retained_mui_utf8.h"

#include "core/log.h"
#include "core/path_utils.h"

#include <ft2build.h>
#include FT_COLOR_H
#include FT_FREETYPE_H
#include "hb-ft.h"
#include "hb.h"
#include "unicode/ubidi.h"
#include "unicode/ubrk.h"
#include "unicode/uclean.h"
#include "unicode/udata.h"
#include "unicode/ustring.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace snt::ui {

using detail::decode_utf8;

namespace {

bool is_cjk_codepoint(uint32_t cp) {
    return (cp >= 0x3400u && cp <= 0x4DBFu) ||
           (cp >= 0x4E00u && cp <= 0x9FFFu) ||
           (cp >= 0xF900u && cp <= 0xFAFFu);
}

bool is_emoji_codepoint(uint32_t cp) {
    return (cp >= 0x1F000u && cp <= 0x1FAFFu) ||
           (cp >= 0x2600u && cp <= 0x27BFu);
}

// Joiners, variation selectors and combining marks stay with the preceding
// face so HarfBuzz receives an intact grapheme (notably emoji ZWJ sequences).
bool continues_font_run(uint32_t cp) {
    return cp == 0x200Du ||
           (cp >= 0x0300u && cp <= 0x036Fu) ||
           (cp >= 0x1AB0u && cp <= 0x1AFFu) ||
           (cp >= 0x1DC0u && cp <= 0x1DFFu) ||
           (cp >= 0x20D0u && cp <= 0x20FFu) ||
           (cp >= 0xFE00u && cp <= 0xFE0Fu) ||
           (cp >= 0xE0100u && cp <= 0xE01EFu);
}


}  // namespace

namespace {

struct IcuDataState {
    std::vector<uint8_t> bytes;
    bool initialized = false;
    std::string error;
};

IcuDataState& icu_data_state() {
    static IcuDataState state;
    return state;
}

bool initialize_icu_data(const snt::core::RuntimePathResolver& paths,
                         const TextEngineConfig& config,
                         std::string& error) {
    auto& state = icu_data_state();
    if (state.initialized) return true;


    const std::string path = paths.resolve_engine(config.icu_data_path);
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        error = "ICU common-data file is unavailable: " + path;
        return false;
    }
    const auto size = static_cast<size_t>(input.tellg());
    if (size == 0) {
        error = "ICU common-data file is empty: " + path;
        return false;
    }
    state.bytes.resize(size);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(state.bytes.data()), static_cast<std::streamsize>(size));
    if (!input) {
        error = "Failed to read ICU common-data file: " + path;
        state.bytes.clear();
        return false;
    }

    UErrorCode status = U_ZERO_ERROR;
    udata_setCommonData(state.bytes.data(), &status);
    if (U_FAILURE(status)) {
        error = "udata_setCommonData failed: " + std::to_string(status);
        state.bytes.clear();
        return false;
    }
    u_init(&status);
    if (U_FAILURE(status)) {
        error = "u_init failed: " + std::to_string(status);
        state.bytes.clear();
        return false;
    }

    state.initialized = true;
    return true;
}

std::vector<UChar> utf16_from_utf8(std::string_view text) {
    UErrorCode status = U_ZERO_ERROR;
    int32_t length = 0;
    u_strFromUTF8(nullptr, 0, &length, text.data(),
                  static_cast<int32_t>(text.size()), &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) return {};

    std::vector<UChar> result(static_cast<size_t>(length) + 1u, 0);
    status = U_ZERO_ERROR;
    u_strFromUTF8(result.data(), static_cast<int32_t>(result.size()), &length,
                  text.data(), static_cast<int32_t>(text.size()), &status);
    if (U_FAILURE(status)) return {};
    result.resize(static_cast<size_t>(length));
    return result;
}

}  // namespace


// Unicode glyph atlas + shaping implementation. The atlas owns no platform
// state: FreeType rasterizes shaped glyph IDs, HarfBuzz owns advances, and the
// Vulkan backend consumes only UiGlyphAtlas's RGBA revision stream.
struct UnicodeTextEngine::Impl {
    static constexpr uint32_t kAtlasPadding = 1;
    static constexpr uint32_t kSdfSpread = 6;

    struct FontFace {
        FT_Face face = nullptr;
        hb_font_t* harfbuzz_font = nullptr;
        uint32_t index = 0;
        bool color = false;

        ~FontFace() {
            if (harfbuzz_font) hb_font_destroy(harfbuzz_font);
            if (face) FT_Done_Face(face);
        }

        FontFace() = default;
        FontFace(const FontFace&) = delete;
        FontFace& operator=(const FontFace&) = delete;
        FontFace(FontFace&&) = delete;
        FontFace& operator=(FontFace&&) = delete;
    };

    struct RasterGlyph {
        int32_t bearing_x = 0;
        int32_t bearing_y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        bool drawable = false;
        bool color = false;
    };

    FT_Library freetype = nullptr;
    std::vector<std::unique_ptr<FontFace>> fonts;
    std::shared_ptr<UiGlyphAtlas> atlas = [] {
        auto result = std::make_shared<UiGlyphAtlas>();
        result->rgba.assign(static_cast<size_t>(result->width) * result->height * 4u, 0u);
        return result;
    }();
    std::unordered_map<uint64_t, RasterGlyph> glyphs;
    uint32_t pack_x = kAtlasPadding;
    uint32_t pack_y = kAtlasPadding;
    uint32_t pack_row_height = 0;
    bool atlas_exhausted_logged = false;

    ~Impl() {
        fonts.clear();
        if (freetype) FT_Done_FreeType(freetype);
    }

    FontFace* font_for(uint32_t codepoint) const {
        for (const auto& font : fonts) {
            if (FT_Get_Char_Index(font->face, codepoint) != 0) return font.get();
        }
        return fonts.empty() ? nullptr : fonts.front().get();
    }

    bool allocate(uint32_t width, uint32_t height, uint32_t& x, uint32_t& y) {
        const uint32_t atlas_width = atlas->width;
        const uint32_t atlas_height = atlas->height;
        if (width + kAtlasPadding * 2u > atlas_width ||
            height + kAtlasPadding * 2u > atlas_height) {
            return false;
        }
        if (pack_x + width + kAtlasPadding > atlas_width) {
            pack_x = kAtlasPadding;
            pack_y += pack_row_height + kAtlasPadding;
            pack_row_height = 0;
        }
        if (pack_y + height + kAtlasPadding > atlas_height) return false;

        x = pack_x;
        y = pack_y;
        pack_x += width + kAtlasPadding;
        pack_row_height = std::max(pack_row_height, height);
        return true;
    }

    static uint64_t glyph_key(const FontFace& face, uint32_t glyph_id, uint32_t pixel_size) {
        return (static_cast<uint64_t>(face.index) << 48u) |
               (static_cast<uint64_t>(pixel_size) << 32u) |
               glyph_id;
    }

    const RasterGlyph* rasterize(FontFace& font, uint32_t glyph_id, uint32_t pixel_size) {
        const uint64_t key = glyph_key(font, glyph_id, pixel_size);
        if (const auto it = glyphs.find(key); it != glyphs.end()) return &it->second;

        RasterGlyph output;
        const FT_Int32 load_flags = FT_LOAD_DEFAULT | (font.color ? FT_LOAD_COLOR : 0);
        if (FT_Load_Glyph(font.face, glyph_id, load_flags) != 0) {
            SNT_LOG_ERROR("MUI failed to load FreeType glyph id=%u from face=%u",
                          glyph_id, font.index);
            return &glyphs.emplace(key, output).first->second;
        }
        FT_GlyphSlot slot = font.face->glyph;
        if (slot->format != FT_GLYPH_FORMAT_BITMAP &&
            FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
            SNT_LOG_ERROR("MUI failed to rasterize FreeType glyph id=%u from face=%u",
                          glyph_id, font.index);
            return &glyphs.emplace(key, output).first->second;
        }

        const FT_Bitmap& bitmap = slot->bitmap;
        if (bitmap.width == 0 || bitmap.rows == 0 || !bitmap.buffer) {
            return &glyphs.emplace(key, output).first->second;
        }

        const bool is_color = bitmap.pixel_mode == FT_PIXEL_MODE_BGRA;
        const uint32_t source_width = bitmap.width;
        const uint32_t source_height = bitmap.rows;
        const uint32_t output_width = is_color ? source_width : source_width + kSdfSpread * 2u;
        const uint32_t output_height = is_color ? source_height : source_height + kSdfSpread * 2u;
        uint32_t atlas_x = 0;
        uint32_t atlas_y = 0;
        if (!allocate(output_width, output_height, atlas_x, atlas_y)) {
            if (!atlas_exhausted_logged) {
                SNT_LOG_ERROR("MUI dynamic glyph atlas is full (%ux%u); text glyph was rejected",
                              atlas->width, atlas->height);
                atlas_exhausted_logged = true;
            }
            return &glyphs.emplace(key, output).first->second;
        }

        output.bearing_x = slot->bitmap_left - (is_color ? 0 : static_cast<int32_t>(kSdfSpread));
        output.bearing_y = slot->bitmap_top + (is_color ? 0 : static_cast<int32_t>(kSdfSpread));
        output.width = output_width;
        output.height = output_height;
        output.u0 = static_cast<float>(atlas_x) / static_cast<float>(atlas->width);
        output.v0 = static_cast<float>(atlas_y) / static_cast<float>(atlas->height);
        output.u1 = static_cast<float>(atlas_x + output_width) / static_cast<float>(atlas->width);
        output.v1 = static_cast<float>(atlas_y + output_height) / static_cast<float>(atlas->height);
        output.drawable = true;
        output.color = is_color;

        const int pitch = bitmap.pitch;
        const int abs_pitch = pitch < 0 ? -pitch : pitch;
        const auto source_pixel = [&](uint32_t x, uint32_t y) -> const uint8_t* {
            const uint32_t source_y = pitch >= 0 ? y : source_height - 1u - y;
            return bitmap.buffer + static_cast<size_t>(source_y) * abs_pitch +
                   (is_color ? static_cast<size_t>(x) * 4u : x);
        };
        const auto destination_pixel = [&](uint32_t x, uint32_t y) -> uint8_t* {
            return atlas->rgba.data() +
                   (static_cast<size_t>(atlas_y + y) * atlas->width + atlas_x + x) * 4u;
        };

        if (is_color) {
            for (uint32_t y = 0; y < source_height; ++y) {
                for (uint32_t x = 0; x < source_width; ++x) {
                    const uint8_t* source = source_pixel(x, y);
                    uint8_t* destination = destination_pixel(x, y);
                    destination[0] = source[2];
                    destination[1] = source[1];
                    destination[2] = source[0];
                    destination[3] = source[3];
                }
            }
        } else if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
            std::vector<uint8_t> mask(static_cast<size_t>(output_width) * output_height, 0u);
            for (uint32_t y = 0; y < source_height; ++y) {
                for (uint32_t x = 0; x < source_width; ++x) {
                    mask[static_cast<size_t>(y + kSdfSpread) * output_width + x + kSdfSpread] =
                        source_pixel(x, y)[0] >= 128u ? 1u : 0u;
                }
            }
            const int spread = static_cast<int>(kSdfSpread);
            for (uint32_t y = 0; y < output_height; ++y) {
                for (uint32_t x = 0; x < output_width; ++x) {
                    const bool inside = mask[static_cast<size_t>(y) * output_width + x] != 0;
                    float nearest = static_cast<float>(spread);
                    for (int dy = -spread; dy <= spread; ++dy) {
                        for (int dx = -spread; dx <= spread; ++dx) {
                            const int sample_x = static_cast<int>(x) + dx;
                            const int sample_y = static_cast<int>(y) + dy;
                            if (sample_x < 0 || sample_y < 0 ||
                                sample_x >= static_cast<int>(output_width) ||
                                sample_y >= static_cast<int>(output_height)) continue;
                            const bool sample_inside = mask[static_cast<size_t>(sample_y) * output_width + sample_x] != 0;
                            if (sample_inside == inside) continue;
                            nearest = std::min(nearest, std::sqrt(static_cast<float>(dx * dx + dy * dy)));
                        }
                    }
                    const float signed_distance = inside
                        ? 0.5f + nearest / (2.0f * static_cast<float>(spread))
                        : 0.5f - nearest / (2.0f * static_cast<float>(spread));
                    uint8_t* destination = destination_pixel(x, y);
                    destination[0] = 255u;
                    destination[1] = 255u;
                    destination[2] = 255u;
                    destination[3] = static_cast<uint8_t>(std::clamp(
                        std::round(signed_distance * 255.0f), 0.0f, 255.0f));
                }
            }
        } else {
            SNT_LOG_ERROR("MUI glyph id=%u uses unsupported FreeType pixel mode=%u",
                          glyph_id, bitmap.pixel_mode);
            output.drawable = false;
        }

        ++atlas->revision;
        return &glyphs.emplace(key, output).first->second;
    }
};

UnicodeTextEngine::UnicodeTextEngine(const snt::core::RuntimePathResolver& paths,
                                     TextEngineConfig config)
    : config_(std::move(config)),
      impl_(std::make_unique<Impl>()) {
    if (!initialize_icu_data(paths, config_, initialization_error_)) return;

    if (FT_Init_FreeType(&impl_->freetype) != 0) {
        initialization_error_ = "FT_Init_FreeType failed";
        return;
    }

    for (const auto& path : config_.font_paths) {
        if (path.empty()) continue;
        FT_Face face = nullptr;
        if (FT_New_Face(impl_->freetype, path.c_str(), 0, &face) != 0) {
            SNT_LOG_WARN("MUI font family entry could not be loaded: %s", path.c_str());
            continue;
        }
        auto loaded = std::make_unique<Impl::FontFace>();
        loaded->face = face;
        loaded->harfbuzz_font = hb_ft_font_create_referenced(face);
        loaded->index = static_cast<uint32_t>(impl_->fonts.size());
        loaded->color = FT_HAS_COLOR(face) != 0;
        impl_->fonts.push_back(std::move(loaded));
    }
    if (impl_->fonts.empty()) {
        initialization_error_ = "No configured MUI font could be loaded";
        return;
    }

    caps_.harfbuzz = true;
    caps_.icu = true;
    caps_.bidi = true;
    caps_.sdf = true;
    caps_.color_emoji = std::any_of(impl_->fonts.begin(), impl_->fonts.end(),
                                    [](const auto& font) { return font->color; });
    if (config_.require_color_emoji && !caps_.color_emoji) {
        initialization_error_ = "No configured font exposes a color-emoji face";
        return;
    }
    available_ = true;
    SNT_LOG_INFO("MUI Unicode text backend ready: faces=%zu atlas=%ux%u",
                 impl_->fonts.size(), impl_->atlas->width, impl_->atlas->height);
}

UnicodeTextEngine::~UnicodeTextEngine() = default;

TextLayout UnicodeTextEngine::shape(std::string_view text, const TextStyle& style) {
    TextLayout layout;
    if (!available_) {
        if (!logged_unavailable_) {
            SNT_LOG_ERROR("MUI UnicodeTextEngine unavailable: %s",
                          initialization_error_.c_str());
            logged_unavailable_ = true;
        }
        return layout;
    }
    layout.glyph_atlas = impl_->atlas;

    const auto utf16 = utf16_from_utf8(text);
    if (!text.empty() && utf16.empty()) {
        SNT_LOG_ERROR("MUI text contains invalid UTF-8; refusing to shape it");
        return layout;
    }

    layout.direction = TextDirection::LeftToRight;

    int32_t grapheme_count = 0;
    if (!utf16.empty()) {
        UErrorCode status = U_ZERO_ERROR;
        UBreakIterator* breaker = ubrk_open(UBRK_CHARACTER, config_.locale.c_str(),
                                            utf16.data(), static_cast<int32_t>(utf16.size()),
                                            &status);
        if (breaker && U_SUCCESS(status)) {
            for (int32_t boundary = ubrk_first(breaker);
                 boundary != UBRK_DONE;
                 boundary = ubrk_next(breaker)) {
                if (boundary > 0) ++grapheme_count;
            }
        }
        if (breaker) ubrk_close(breaker);
    }

    const uint32_t pixel_size = static_cast<uint32_t>(std::max(1.0f, std::round(style.size_px)));
    const float line_height = std::max(style.size_px * 1.25f, style.size_px);
    const float baseline = style.size_px;
    float line_width = 0.0f;
    float max_width = 0.0f;
    float line_y = 0.0f;
    int32_t line_count = 1;
    auto append_run = [&](std::string_view run, Impl::FontFace& face, hb_direction_t direction) {
        if (run.empty()) return;
        if (FT_Set_Pixel_Sizes(face.face, 0, pixel_size) != 0) {
            SNT_LOG_ERROR("MUI failed to set pixel size=%u for font face=%u", pixel_size, face.index);
            return;
        }
        hb_ft_font_changed(face.harfbuzz_font);

        hb_buffer_t* buffer = hb_buffer_create();
        hb_buffer_set_direction(buffer, direction);
        hb_buffer_add_utf8(buffer, run.data(), static_cast<int>(run.size()), 0,
                           static_cast<int>(run.size()));
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(face.harfbuzz_font, buffer, nullptr, 0);

        unsigned int glyph_count = 0;
        const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
        const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
        float run_width = 0.0f;
        for (unsigned int i = 0; i < glyph_count; ++i) {
            run_width += std::abs(static_cast<float>(positions[i].x_advance) / 64.0f);
        }
        float pen_x = line_width + (direction == HB_DIRECTION_RTL ? run_width : 0.0f);
        for (unsigned int i = 0; i < glyph_count; ++i) {
            const float advance = static_cast<float>(positions[i].x_advance) / 64.0f;
            const float offset_x = static_cast<float>(positions[i].x_offset) / 64.0f;
            const float offset_y = static_cast<float>(positions[i].y_offset) / 64.0f;
            if (direction == HB_DIRECTION_RTL) pen_x += advance;
            const float glyph_origin_x = pen_x + offset_x;

            const size_t byte_offset = std::min<size_t>(infos[i].cluster, run.size());
            size_t decode_offset = byte_offset;
            const uint32_t codepoint = decode_utf8(run, decode_offset);
            const Impl::RasterGlyph* raster = impl_->rasterize(face, infos[i].codepoint, pixel_size);

            TextCluster cluster;
            cluster.utf8 = std::string(run.substr(byte_offset, decode_offset - byte_offset));
            cluster.first_codepoint = codepoint;
            cluster.advance = std::abs(advance);
            cluster.is_emoji = is_emoji_codepoint(codepoint);
            cluster.is_cjk = is_cjk_codepoint(codepoint);
            cluster.requires_color = raster && raster->color;
            layout.contains_emoji = layout.contains_emoji || cluster.is_emoji;
            layout.contains_cjk = layout.contains_cjk || cluster.is_cjk;
            layout.clusters.push_back(std::move(cluster));

            if (raster && raster->drawable) {
                TextGlyph glyph;
                glyph.glyph_id = infos[i].codepoint;
                glyph.x = glyph_origin_x + static_cast<float>(raster->bearing_x);
                glyph.y = line_y + baseline - static_cast<float>(raster->bearing_y) - offset_y;
                glyph.width = static_cast<float>(raster->width);
                glyph.height = static_cast<float>(raster->height);
                glyph.u0 = raster->u0;
                glyph.v0 = raster->v0;
                glyph.u1 = raster->u1;
                glyph.v1 = raster->v1;
                glyph.drawable = true;
                glyph.color = raster->color;
                layout.glyphs.push_back(glyph);
            }
            if (direction != HB_DIRECTION_RTL) pen_x += advance;
        }
        line_width += run_width;
        hb_buffer_destroy(buffer);
    };

    auto append_visual_run = [&](std::string_view run, hb_direction_t direction) {
        struct FontRun {
            std::string_view bytes;
            Impl::FontFace* face = nullptr;
        };
        std::vector<FontRun> font_runs;
        size_t run_start = 0;
        Impl::FontFace* run_face = nullptr;
        for (size_t offset = 0; offset < run.size();) {
            const size_t codepoint_start = offset;
            const uint32_t codepoint = decode_utf8(run, offset);
            Impl::FontFace* face = (run_face && continues_font_run(codepoint))
                ? run_face : impl_->font_for(codepoint);
            if (!run_face) {
                run_face = face;
                run_start = codepoint_start;
            } else if (face != run_face) {
                font_runs.push_back({run.substr(run_start, codepoint_start - run_start), run_face});
                run_face = face;
                run_start = codepoint_start;
            }
        }
        if (run_face) font_runs.push_back({run.substr(run_start), run_face});

        if (direction == HB_DIRECTION_RTL) {
            for (auto it = font_runs.rbegin(); it != font_runs.rend(); ++it) {
                append_run(it->bytes, *it->face, direction);
            }
        } else {
            for (const FontRun& font_run : font_runs) {
                append_run(font_run.bytes, *font_run.face, direction);
            }
        }
    };

    auto append_bidi_line = [&](std::string_view line, bool first_line) {
        if (line.empty()) return;
        const auto line_utf16 = utf16_from_utf8(line);
        if (line_utf16.empty()) {
            SNT_LOG_ERROR("MUI line contains invalid UTF-8; refusing to shape it");
            return;
        }

        std::vector<size_t> byte_offsets(line_utf16.size() + 1u, line.size());
        size_t byte_offset = 0;
        int32_t utf16_offset = 0;
        byte_offsets[0] = 0;
        while (byte_offset < line.size() && utf16_offset < static_cast<int32_t>(line_utf16.size())) {
            const size_t codepoint_start = byte_offset;
            const uint32_t codepoint = decode_utf8(line, byte_offset);
            const int32_t units = codepoint > 0xFFFFu ? 2 : 1;
            for (int32_t unit = 0; unit < units && utf16_offset + unit < static_cast<int32_t>(byte_offsets.size()); ++unit) {
                byte_offsets[utf16_offset + unit] = codepoint_start;
            }
            utf16_offset += units;
            if (utf16_offset < static_cast<int32_t>(byte_offsets.size())) {
                byte_offsets[utf16_offset] = byte_offset;
            }
        }

        UErrorCode status = U_ZERO_ERROR;
        UBiDi* bidi = ubidi_openSized(static_cast<int32_t>(line_utf16.size()), 0, &status);
        if (!bidi || U_FAILURE(status)) {
            if (bidi) ubidi_close(bidi);
            SNT_LOG_ERROR("MUI failed to allocate ICU BiDi state: %d", status);
            return;
        }
        ubidi_setPara(bidi, line_utf16.data(), static_cast<int32_t>(line_utf16.size()),
                      UBIDI_DEFAULT_LTR, nullptr, &status);
        if (U_FAILURE(status)) {
            SNT_LOG_ERROR("MUI ICU BiDi analysis failed: %d", status);
            ubidi_close(bidi);
            return;
        }
        if (first_line && ubidi_getDirection(bidi) == UBIDI_RTL) {
            layout.direction = TextDirection::RightToLeft;
        }

        const int32_t visual_run_count = ubidi_countRuns(bidi, &status);
        if (U_FAILURE(status)) {
            SNT_LOG_ERROR("MUI ICU BiDi visual-run analysis failed: %d", status);
            ubidi_close(bidi);
            return;
        }
        for (int32_t visual_index = 0; visual_index < visual_run_count; ++visual_index) {
            int32_t logical_start = 0;
            int32_t length = 0;
            const UBiDiDirection direction = ubidi_getVisualRun(
                bidi, visual_index, &logical_start, &length);
            const int32_t logical_end = logical_start + length;
            if (logical_start < 0 || logical_end < logical_start ||
                logical_end >= static_cast<int32_t>(byte_offsets.size())) {
                SNT_LOG_ERROR("MUI ICU BiDi returned an invalid visual run");
                continue;
            }
            const size_t start_byte = byte_offsets[logical_start];
            const size_t end_byte = byte_offsets[logical_end];
            if (end_byte > start_byte) {
                append_visual_run(line.substr(start_byte, end_byte - start_byte),
                                  direction == UBIDI_RTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            }
        }
        ubidi_close(bidi);
    };

    size_t line_start = 0;
    bool first_line = true;
    while (line_start <= text.size()) {
        const size_t line_end = text.find('\n', line_start);
        const size_t line_length = line_end == std::string_view::npos
            ? text.size() - line_start : line_end - line_start;
        append_bidi_line(text.substr(line_start, line_length), first_line);
        first_line = false;
        max_width = std::max(max_width, line_width);
        if (line_end == std::string_view::npos) break;

        TextCluster newline;
        newline.utf8 = "\n";
        newline.first_codepoint = '\n';
        layout.clusters.push_back(std::move(newline));
        line_width = 0.0f;
        line_y += line_height;
        ++line_count;
        line_start = line_end + 1u;
    }
    max_width = std::max(max_width, line_width);
    layout.size = {max_width, line_height * static_cast<float>(line_count)};
    (void)grapheme_count;  // Break iteration validates the full Unicode path.
    return layout;
}


}  // namespace snt::ui