#define SNT_LOG_CHANNEL "ui"
#include "retained_mui.h"

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
#include <cstdio>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace snt::ui {

namespace {

bool is_rtl_codepoint(uint32_t cp) {
    return (cp >= 0x0590u && cp <= 0x08FFu) ||
           (cp >= 0xFB1Du && cp <= 0xFDFFu) ||
           (cp >= 0xFE70u && cp <= 0xFEFFu);
}

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

uint32_t decode_utf8(std::string_view s, size_t& offset) {
    const auto read = [&](size_t i) -> uint8_t {
        return static_cast<uint8_t>(s[offset + i]);
    };

    if (offset >= s.size()) return 0;

    const uint8_t b0 = read(0);
    if (b0 < 0x80u) {
        ++offset;
        return b0;
    }
    if ((b0 & 0xE0u) == 0xC0u && offset + 1 < s.size()) {
        const uint32_t cp = ((b0 & 0x1Fu) << 6) | (read(1) & 0x3Fu);
        offset += 2;
        return cp;
    }
    if ((b0 & 0xF0u) == 0xE0u && offset + 2 < s.size()) {
        const uint32_t cp = ((b0 & 0x0Fu) << 12) |
                            ((read(1) & 0x3Fu) << 6) |
                            (read(2) & 0x3Fu);
        offset += 3;
        return cp;
    }
    if ((b0 & 0xF8u) == 0xF0u && offset + 3 < s.size()) {
        const uint32_t cp = ((b0 & 0x07u) << 18) |
                            ((read(1) & 0x3Fu) << 12) |
                            ((read(2) & 0x3Fu) << 6) |
                            (read(3) & 0x3Fu);
        offset += 4;
        return cp;
    }

    ++offset;
    return 0xFFFDu;
}

// The retained text path intentionally has no ASCII bitmap implementation.
// Rasterization is performed from shaped FreeType glyph IDs below.

template <typename T>
T* find_view_recursive(std::vector<std::unique_ptr<View>>& children, std::string_view id) {
    for (auto& child : children) {
        if (child->id() == id) return child.get();
        if (auto* group = dynamic_cast<ViewGroup*>(child.get())) {
            if (auto* found = group->find(id)) return found;
        }
    }
    return nullptr;
}

template <typename T>
const T* find_view_recursive(const std::vector<std::unique_ptr<View>>& children, std::string_view id) {
    for (const auto& child : children) {
        if (child->id() == id) return child.get();
        if (const auto* group = dynamic_cast<const ViewGroup*>(child.get())) {
            if (auto* found = group->find(id)) return found;
        }
    }
    return nullptr;
}

std::string binding_value_to_string(const BindingValue& value) {
    if (const auto* s = std::get_if<std::string>(&value)) return *s;
    if (const auto* b = std::get_if<bool>(&value)) return *b ? "true" : "false";
    if (const auto* i = std::get_if<int64_t>(&value)) return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&value)) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.3f", *d);
        return buffer;
    }
    return {};
}
MeasureSpec child_spec(float parent_inner, float requested, float margin_a, float margin_b) {
    const float available = std::max(0.0f, parent_inner - margin_a - margin_b);
    if (requested > 0.0f) return {.size = requested, .mode = MeasureMode::Exactly};
    if (requested == 0.0f) return {.size = available, .mode = MeasureMode::Exactly};
    return {.size = available, .mode = MeasureMode::AtMost};
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

bool initialize_icu_data(const TextEngineConfig& config, std::string& error) {
    auto& state = icu_data_state();
    if (state.initialized) return true;

    snt::core::path_utils::init();
    const std::string path = snt::core::path_utils::resolve(config.icu_data_path);
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

UnicodeTextEngine::UnicodeTextEngine(TextEngineConfig config)
    : config_(std::move(config)),
      impl_(std::make_unique<Impl>()) {
    if (!initialize_icu_data(config_, initialization_error_)) return;

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

    UBiDiDirection bidi_direction = UBIDI_LTR;
    if (!utf16.empty()) {
        UErrorCode status = U_ZERO_ERROR;
        UBiDi* bidi = ubidi_openSized(static_cast<int32_t>(utf16.size()), 0, &status);
        if (bidi && U_SUCCESS(status)) {
            ubidi_setPara(bidi, utf16.data(), static_cast<int32_t>(utf16.size()),
                          UBIDI_DEFAULT_LTR, nullptr, &status);
            if (U_SUCCESS(status)) bidi_direction = ubidi_getDirection(bidi);
        }
        if (bidi) ubidi_close(bidi);
    }
    layout.direction = bidi_direction == UBIDI_RTL
        ? TextDirection::RightToLeft : TextDirection::LeftToRight;

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
    const hb_direction_t hb_direction = layout.direction == TextDirection::RightToLeft
        ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;

    auto append_run = [&](std::string_view run, Impl::FontFace& face) {
        if (run.empty()) return;
        if (FT_Set_Pixel_Sizes(face.face, 0, pixel_size) != 0) {
            SNT_LOG_ERROR("MUI failed to set pixel size=%u for font face=%u", pixel_size, face.index);
            return;
        }
        hb_ft_font_changed(face.harfbuzz_font);

        hb_buffer_t* buffer = hb_buffer_create();
        hb_buffer_set_direction(buffer, hb_direction);
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
        float pen_x = line_width + (hb_direction == HB_DIRECTION_RTL ? run_width : 0.0f);
        for (unsigned int i = 0; i < glyph_count; ++i) {
            const float advance = static_cast<float>(positions[i].x_advance) / 64.0f;
            const float offset_x = static_cast<float>(positions[i].x_offset) / 64.0f;
            const float offset_y = static_cast<float>(positions[i].y_offset) / 64.0f;
            if (hb_direction == HB_DIRECTION_RTL) pen_x += advance;
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
            if (hb_direction != HB_DIRECTION_RTL) pen_x += advance;
        }
        line_width += run_width;
        hb_buffer_destroy(buffer);
    };

    size_t run_start = 0;
    Impl::FontFace* run_face = nullptr;
    for (size_t offset = 0; offset < text.size();) {
        const size_t codepoint_start = offset;
        const uint32_t codepoint = decode_utf8(text, offset);
        if (codepoint == '\n') {
            if (run_face) append_run(text.substr(run_start, codepoint_start - run_start), *run_face);
            TextCluster newline;
            newline.utf8 = "\n";
            newline.first_codepoint = '\n';
            layout.clusters.push_back(std::move(newline));
            max_width = std::max(max_width, line_width);
            line_width = 0.0f;
            line_y += line_height;
            ++line_count;
            run_start = offset;
            run_face = nullptr;
            continue;
        }

        Impl::FontFace* face = (run_face && continues_font_run(codepoint))
            ? run_face : impl_->font_for(codepoint);
        if (!run_face) {
            run_face = face;
            run_start = codepoint_start;
        } else if (face != run_face) {
            append_run(text.substr(run_start, codepoint_start - run_start), *run_face);
            run_face = face;
            run_start = codepoint_start;
        }
    }
    if (run_face) append_run(text.substr(run_start), *run_face);

    max_width = std::max(max_width, line_width);
    layout.size = {max_width, line_height * static_cast<float>(line_count)};
    (void)grapheme_count;  // Break iteration validates the full Unicode path.
    return layout;
}void ViewModel::set(std::string key, BindingValue value) {
    const std::string stable_key = key;
    values_[std::move(key)] = value;
    auto it = observers_.find(stable_key);
    if (it == observers_.end()) return;
    for (auto& observer : it->second) {
        if (observer) observer(stable_key, values_[stable_key]);
    }
}

const BindingValue* ViewModel::get(std::string_view key) const {
    auto it = values_.find(std::string(key));
    return it == values_.end() ? nullptr : &it->second;
}

void ViewModel::bind(std::string key, Observer observer) {
    if (!observer) return;
    auto& list = observers_[key];
    list.push_back(observer);
    auto value_it = values_.find(key);
    if (value_it != values_.end()) {
        list.back()(key, value_it->second);
    }
}

void ViewModel::set_command(std::string name, Command command) {
    commands_[std::move(name)] = std::move(command);
}

bool ViewModel::invoke(std::string_view name, BindingValue payload) {
    auto it = commands_.find(std::string(name));
    if (it == commands_.end() || !it->second) {
        SNT_LOG_WARN("ViewModel command '%.*s' is not registered",
                     static_cast<int>(name.size()), name.data());
        return false;
    }
    it->second(payload);
    return true;
}

void Arc2DCommandBuffer::clear() {
    commands_.clear();
}

void Arc2DCommandBuffer::rect(Rect rect, Color color, float radius) {
    commands_.push_back(DrawRectCommand{rect, color, radius});
}

void Arc2DCommandBuffer::text(Rect rect, std::string text, TextStyle style, TextLayout layout) {
    commands_.push_back(DrawTextCommand{rect, std::move(text), style, std::move(layout)});
}

void Arc2DCommandBuffer::image(Rect rect, std::string image_key, Color tint) {
    commands_.push_back(DrawImageCommand{rect, std::move(image_key), tint});
}

UiDrawData Arc2DRenderer::build_draw_data(const Arc2DCommandBuffer& commands) const {
    UiDrawData out;
    for (const auto& cmd : commands.commands()) {
        if (const auto* rect = std::get_if<DrawRectCommand>(&cmd)) {
            append_rect(out, rect->rect, rect->color);
        } else if (const auto* image = std::get_if<DrawImageCommand>(&cmd)) {
            append_rect(out, image->rect, image->tint);
        } else if (const auto* text = std::get_if<DrawTextCommand>(&cmd)) {
            append_glyph_text(out, *text);
        }
    }
    return out;
}

void Arc2DRenderer::append_rect(UiDrawData& out, Rect rect, Color color) {
    if (rect.size.x <= 0.0f || rect.size.y <= 0.0f) return;
    if (out.vertices.size() + 4 > 0xFFFFu) {
        SNT_LOG_WARN("Arc2DRenderer draw buffer overflow; dropping rect");
        return;
    }

    const uint16_t base = static_cast<uint16_t>(out.vertices.size());
    UiVertex v{};
    v.uv[0] = -1.0f;
    v.uv[1] = -1.0f;
    v.color[0] = color.r;
    v.color[1] = color.g;
    v.color[2] = color.b;
    v.color[3] = color.a;

    const float x0 = rect.pos.x;
    const float y0 = rect.pos.y;
    const float x1 = rect.pos.x + rect.size.x;
    const float y1 = rect.pos.y + rect.size.y;

    v.position[0] = x0; v.position[1] = y0; out.vertices.push_back(v);
    v.position[0] = x0; v.position[1] = y1; out.vertices.push_back(v);
    v.position[0] = x1; v.position[1] = y1; out.vertices.push_back(v);
    v.position[0] = x1; v.position[1] = y0; out.vertices.push_back(v);

    out.indices.push_back(base + 0);
    out.indices.push_back(base + 1);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 3);
}

void Arc2DRenderer::append_glyph_text(UiDrawData& out, const DrawTextCommand& text) {
    if (!text.layout.glyph_atlas) {
        SNT_LOG_ERROR("MUI text command is missing its Unicode glyph atlas");
        return;
    }
    if (!out.glyph_atlas) {
        out.glyph_atlas = text.layout.glyph_atlas;
    } else if (out.glyph_atlas.get() != text.layout.glyph_atlas.get()) {
        SNT_LOG_ERROR("MUI frame combines text from different glyph atlases; rejecting batch");
        return;
    }

    for (const TextGlyph& glyph : text.layout.glyphs) {
        if (!glyph.drawable || glyph.width <= 0.0f || glyph.height <= 0.0f) continue;
        if (out.vertices.size() + 4 > 0xFFFFu) {
            SNT_LOG_WARN("Arc2DRenderer draw buffer overflow; dropping glyph batch");
            return;
        }

        const uint16_t base = static_cast<uint16_t>(out.vertices.size());
        const Color color = glyph.color ? Color{255, 255, 255, 255} : text.style.color;
        UiVertex vertex{};
        vertex.color[0] = color.r;
        vertex.color[1] = color.g;
        vertex.color[2] = color.b;
        vertex.color[3] = color.a;
        vertex.texture_mode = glyph.color
            ? UiTextureMode::ColorGlyph
            : UiTextureMode::SignedDistanceGlyph;

        const float x0 = text.rect.pos.x + glyph.x;
        const float y0 = text.rect.pos.y + glyph.y;
        const float x1 = x0 + glyph.width;
        const float y1 = y0 + glyph.height;
        vertex.position[0] = x0; vertex.position[1] = y0;
        vertex.uv[0] = glyph.u0; vertex.uv[1] = glyph.v0; out.vertices.push_back(vertex);
        vertex.position[0] = x0; vertex.position[1] = y1;
        vertex.uv[0] = glyph.u0; vertex.uv[1] = glyph.v1; out.vertices.push_back(vertex);
        vertex.position[0] = x1; vertex.position[1] = y1;
        vertex.uv[0] = glyph.u1; vertex.uv[1] = glyph.v1; out.vertices.push_back(vertex);
        vertex.position[0] = x1; vertex.position[1] = y0;
        vertex.uv[0] = glyph.u1; vertex.uv[1] = glyph.v0; out.vertices.push_back(vertex);

        out.indices.push_back(base + 0);
        out.indices.push_back(base + 1);
        out.indices.push_back(base + 2);
        out.indices.push_back(base + 0);
        out.indices.push_back(base + 2);
        out.indices.push_back(base + 3);
    }
}

View::View(std::string id)
    : id_(std::move(id)) {}

void View::set_background(Color color, float radius) {
    background_ = DrawRectCommand{{}, color, radius};
}

void View::bind_text(ViewModel& model, std::string key) {
    set_bound_text_key(key);
    model.bind(key, [this](std::string_view, const BindingValue& value) {
        if (auto* text = dynamic_cast<TextView*>(this)) {
            text->set_text(binding_value_to_string(value));
        }
    });
}

void View::measure(MeasureSpec width, MeasureSpec height, TextEngine&) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, 0.0f);
    measured_size_.y = resolve_axis(layout_params_.height, height, 0.0f);
}

void View::layout(Rect bounds) {
    bounds_ = bounds;
}

void View::paint(Arc2DCommandBuffer& out, TextEngine&) const {
    if (visibility_ != Visibility::Visible) return;
    if (background_) {
        out.rect(bounds_, background_->color, background_->radius);
    }
}

float View::resolve_axis(float requested, MeasureSpec spec, float desired) {
    if (requested > 0.0f) return requested;
    if (requested == 0.0f && spec.mode != MeasureMode::Unspecified) return spec.size;
    return clamp_axis(desired, spec);
}

float View::clamp_axis(float value, MeasureSpec spec) {
    switch (spec.mode) {
        case MeasureMode::Exactly: return spec.size;
        case MeasureMode::AtMost: return std::min(value, spec.size);
        case MeasureMode::Unspecified: return value;
    }
    return value;
}

TextView::TextView(std::string id)
    : View(std::move(id)) {}

void TextView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    cached_layout_ = text_engine.shape(text_, style_);
    dirty_layout_ = false;
    const float desired_w = cached_layout_.size.x + layout_params_.margin.left + layout_params_.margin.right;
    const float desired_h = cached_layout_.size.y + layout_params_.margin.top + layout_params_.margin.bottom;
    measured_size_.x = resolve_axis(layout_params_.width, width, desired_w);
    measured_size_.y = resolve_axis(layout_params_.height, height, desired_h);
}

void TextView::paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const {
    View::paint(out, text_engine);
    if (visibility_ != Visibility::Visible || text_.empty()) return;
    if (dirty_layout_) {
        cached_layout_ = text_engine.shape(text_, style_);
        dirty_layout_ = false;
    }
    out.text(bounds_, text_, style_, cached_layout_);
}

Button::Button(std::string id)
    : TextView(std::move(id)) {
    set_background({37, 50, 65, 230}, 4.0f);
    TextStyle style = text_style();
    style.color = {240, 245, 255, 255};
    set_text_style(style);
}

bool Button::click(ViewModel& model, BindingValue payload) const {
    if (command_.empty()) return false;
    return model.invoke(command_, std::move(payload));
}

ImageView::ImageView(std::string id)
    : View(std::move(id)) {}

void ImageView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    (void)text_engine;
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, 32.0f);
    measured_size_.y = resolve_axis(layout_params_.height, height, 32.0f);
}

void ImageView::paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const {
    View::paint(out, text_engine);
    if (visibility_ != Visibility::Visible || image_key_.empty()) return;
    out.image(bounds_, image_key_, tint_);
}

SlotView::SlotView(std::string id)
    : View(std::move(id)) {}

void SlotView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    (void)text_engine;
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, 36.0f);
    measured_size_.y = resolve_axis(layout_params_.height, height, 36.0f);
}

void SlotView::paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const {
    if (visibility_ != Visibility::Visible) return;
    const Color base = state_.selected ? Color{84, 128, 176, 255}
                                       : Color{34, 38, 48, 255};
    out.rect(bounds_, base, 3.0f);
    if (!state_.item_key.empty() && state_.count > 0) {
        const Rect icon{
            .pos = {bounds_.pos.x + 4.0f, bounds_.pos.y + 4.0f},
            .size = {std::max(0.0f, bounds_.size.x - 8.0f),
                     std::max(0.0f, bounds_.size.y - 8.0f)},
        };
        out.image(icon, state_.item_key, {255, 255, 255, 255});
        if (state_.count > 1) {
            TextStyle style;
            style.size_px = 11.0f;
            style.color = {255, 255, 255, 255};
            auto text = std::to_string(state_.count);
            auto layout = text_engine.shape(text, style);
            out.text(bounds_, std::move(text), style, std::move(layout));
        }
    }
}

ViewGroup::ViewGroup(std::string id)
    : View(std::move(id)) {}

View& ViewGroup::add_child(std::unique_ptr<View> child) {
    children_.push_back(std::move(child));
    return *children_.back();
}

View* ViewGroup::find(std::string_view id) {
    if (id_ == id) return this;
    return find_view_recursive<View>(children_, id);
}

const View* ViewGroup::find(std::string_view id) const {
    if (id_ == id) return this;
    return find_view_recursive<View>(children_, id);
}

void ViewGroup::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }

    float max_w = 0.0f;
    float max_h = 0.0f;
    for (auto& child : children_) {
        const auto& lp = child->layout_params();
        child->measure(child_spec(width.size, lp.width, lp.margin.left, lp.margin.right),
                       child_spec(height.size, lp.height, lp.margin.top, lp.margin.bottom),
                       text_engine);
        max_w = std::max(max_w, child->measured_size().x + lp.margin.left + lp.margin.right);
        max_h = std::max(max_h, child->measured_size().y + lp.margin.top + lp.margin.bottom);
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, max_w);
    measured_size_.y = resolve_axis(layout_params_.height, height, max_h);
}

void ViewGroup::layout(Rect bounds) {
    View::layout(bounds);
    for (auto& child : children_) {
        const auto& lp = child->layout_params();
        const Rect child_bounds{
            .pos = {bounds.pos.x + lp.margin.left, bounds.pos.y + lp.margin.top},
            .size = {child->measured_size().x, child->measured_size().y},
        };
        child->layout(child_bounds);
    }
}

void ViewGroup::paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const {
    View::paint(out, text_engine);
    if (visibility_ != Visibility::Visible) return;
    for (const auto& child : children_) {
        child->paint(out, text_engine);
    }
}

LinearLayout::LinearLayout(std::string id)
    : ViewGroup(std::move(id)) {}

void LinearLayout::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }

    const float inner_w = width.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, width.size - padding_.left - padding_.right);
    const float inner_h = height.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, height.size - padding_.top - padding_.bottom);

    float major = 0.0f;
    float cross = 0.0f;
    int visible_count = 0;

    for (auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        child->measure(child_spec(inner_w, lp.width, lp.margin.left, lp.margin.right),
                       child_spec(inner_h, lp.height, lp.margin.top, lp.margin.bottom),
                       text_engine);

        const Vec2 m = child->measured_size();
        if (orientation_ == Orientation::Horizontal) {
            major += m.x + lp.margin.left + lp.margin.right;
            cross = std::max(cross, m.y + lp.margin.top + lp.margin.bottom);
        } else {
            major += m.y + lp.margin.top + lp.margin.bottom;
            cross = std::max(cross, m.x + lp.margin.left + lp.margin.right);
        }
        ++visible_count;
    }

    if (visible_count > 1) {
        major += spacing_ * static_cast<float>(visible_count - 1);
    }

    const float desired_w = orientation_ == Orientation::Horizontal
        ? major + padding_.left + padding_.right
        : cross + padding_.left + padding_.right;
    const float desired_h = orientation_ == Orientation::Horizontal
        ? cross + padding_.top + padding_.bottom
        : major + padding_.top + padding_.bottom;

    measured_size_.x = resolve_axis(layout_params_.width, width, desired_w);
    measured_size_.y = resolve_axis(layout_params_.height, height, desired_h);
}

void LinearLayout::layout(Rect bounds) {
    View::layout(bounds);
    float cursor = orientation_ == Orientation::Horizontal
        ? bounds.pos.x + padding_.left
        : bounds.pos.y + padding_.top;

    for (auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        Vec2 pos{};
        if (orientation_ == Orientation::Horizontal) {
            cursor += lp.margin.left;
            pos = {cursor, bounds.pos.y + padding_.top + lp.margin.top};
            cursor += child->measured_size().x + lp.margin.right + spacing_;
        } else {
            cursor += lp.margin.top;
            pos = {bounds.pos.x + padding_.left + lp.margin.left, cursor};
            cursor += child->measured_size().y + lp.margin.bottom + spacing_;
        }
        child->layout({.pos = pos, .size = child->measured_size()});
    }
}

FrameLayout::FrameLayout(std::string id)
    : ViewGroup(std::move(id)) {}

void FrameLayout::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    const float inner_w = width.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, width.size - padding_.left - padding_.right);
    const float inner_h = height.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, height.size - padding_.top - padding_.bottom);

    float max_w = 0.0f;
    float max_h = 0.0f;
    for (auto& child : children_) {
        const auto& lp = child->layout_params();
        child->measure(child_spec(inner_w, lp.width, lp.margin.left, lp.margin.right),
                       child_spec(inner_h, lp.height, lp.margin.top, lp.margin.bottom),
                       text_engine);
        max_w = std::max(max_w, child->measured_size().x + lp.margin.left + lp.margin.right);
        max_h = std::max(max_h, child->measured_size().y + lp.margin.top + lp.margin.bottom);
    }
    measured_size_.x = resolve_axis(layout_params_.width, width,
                                    max_w + padding_.left + padding_.right);
    measured_size_.y = resolve_axis(layout_params_.height, height,
                                    max_h + padding_.top + padding_.bottom);
}

void FrameLayout::layout(Rect bounds) {
    View::layout(bounds);
    for (auto& child : children_) {
        const auto& lp = child->layout_params();
        child->layout({
            .pos = {bounds.pos.x + padding_.left + lp.margin.left,
                    bounds.pos.y + padding_.top + lp.margin.top},
            .size = child->measured_size(),
        });
    }
}

Animation::Animation(float from, float to, float duration_s, Setter setter)
    : from_(from),
      to_(to),
      duration_s_(std::max(duration_s, 0.0001f)),
      setter_(std::move(setter)) {}

bool Animation::tick(float dt) {
    if (finished_) return true;
    elapsed_s_ = std::min(duration_s_, elapsed_s_ + std::max(0.0f, dt));
    const float t = elapsed_s_ / duration_s_;
    const float eased = t * t * (3.0f - 2.0f * t);
    if (setter_) setter_(from_ + (to_ - from_) * eased);
    finished_ = elapsed_s_ >= duration_s_;
    return finished_;
}

void Animator::add(Animation animation) {
    animations_.push_back(std::move(animation));
}

void Animator::update(float dt) {
    for (auto& animation : animations_) {
        animation.tick(dt);
    }
    animations_.erase(
        std::remove_if(animations_.begin(), animations_.end(),
                       [](const Animation& a) { return a.finished(); }),
        animations_.end());
}

UiRuntime::UiRuntime(TextEngineConfig config)
    : text_engine_(std::move(config)) {}

UiFrameResult UiRuntime::build_frame(View& root, Vec2 viewport) {
    root.measure({.size = viewport.x, .mode = MeasureMode::Exactly},
                 {.size = viewport.y, .mode = MeasureMode::Exactly},
                 text_engine_);
    root.layout({.pos = {0.0f, 0.0f}, .size = viewport});

    UiFrameResult result;
    root.paint(result.commands, text_engine_);
    result.draw_data = renderer_.build_draw_data(result.commands);
    return result;
}

}  // namespace snt::ui
