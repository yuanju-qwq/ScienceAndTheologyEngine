// Retained-MUI Unicode text shaping and glyph-atlas interface.

#pragma once

#include "retained_mui_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace snt::core { class RuntimePathResolver; }

namespace snt::ui {

struct TextStyle {
    float size_px = 16.0f;
    Color color{230, 236, 245, 255};
    bool sdf = true;
    bool emoji = true;
};

enum class TextDirection : uint8_t {
    LeftToRight,
    RightToLeft,
};

struct TextCluster {
    std::string utf8;
    uint32_t first_codepoint = 0;
    float advance = 0.0f;
    bool is_emoji = false;
    bool is_cjk = false;
    bool requires_color = false;
};

// A raster-ready HarfBuzz glyph. Metrics are derived from the same
// FreeType face used during shaping; UVs address the dynamic Unicode atlas.
struct TextGlyph {
    uint32_t glyph_id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    bool drawable = false;
    bool color = false;
};

struct TextLayout {
    std::vector<TextCluster> clusters;
    std::vector<TextGlyph> glyphs;
    std::shared_ptr<const UiGlyphAtlas> glyph_atlas;
    Vec2 size{};
    bool contains_emoji = false;
    bool contains_cjk = false;
    TextDirection direction = TextDirection::LeftToRight;
};

struct TextEngineCapabilities {
    bool harfbuzz = false;
    bool icu = false;
    bool bidi = false;
    bool sdf = false;
    bool color_emoji = false;
};

struct TextEngineConfig {
    bool require_harfbuzz = true;
    bool require_icu = true;
    bool require_sdf = true;
    bool require_color_emoji = true;
    // Ordered font family for real glyph coverage, not a compatibility path.
    // All entries participate in HarfBuzz/FreeType shaping and rasterization.
    std::vector<std::string> font_paths{
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/seguiemj.ttf",
    };
    std::string locale = "zh-Hans";
    std::string icu_data_path = "third_party/icu4c/icudt_godot.dat";
};

class TextEngine {
public:
    virtual ~TextEngine() = default;
    virtual const TextEngineCapabilities& capabilities() const = 0;
    virtual TextLayout shape(std::string_view text, const TextStyle& style) = 0;
};

// Production MUI text engine. It has no simplified shaping path: UTF-8 is
// converted to ICU UTF-16, BiDi/grapheme analysis runs through ICU, and each
// font run is shaped by HarfBuzz using a FreeType face. Missing required
// backends leave the engine unavailable rather than silently degrading text.
class UnicodeTextEngine final : public TextEngine {
public:
    // `paths` is borrowed only during construction to load engine-owned ICU
    // data; font paths remain explicit TextEngineConfig entries.
    UnicodeTextEngine(const snt::core::RuntimePathResolver& paths,
                      TextEngineConfig config = {});
    ~UnicodeTextEngine() override;

    UnicodeTextEngine(const UnicodeTextEngine&) = delete;
    UnicodeTextEngine& operator=(const UnicodeTextEngine&) = delete;

    const TextEngineCapabilities& capabilities() const override { return caps_; }
    TextLayout shape(std::string_view text, const TextStyle& style) override;
    bool available() const { return available_; }
    const std::string& initialization_error() const { return initialization_error_; }

private:
    struct Impl;

    TextEngineConfig config_{};
    TextEngineCapabilities caps_{};
    std::unique_ptr<Impl> impl_;
    bool available_ = false;
    bool logged_unavailable_ = false;
    std::string initialization_error_;
};

}  // namespace snt::ui
