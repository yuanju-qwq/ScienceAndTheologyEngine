// UI draw data shared by retained MUI and the Vulkan UI renderer.
//
// This is the engine-native draw contract: retained views emit Arc2D
// commands, Arc2D lowers supported primitives to this vertex/index stream,
// and MuiRenderer records it into the current Vulkan command buffer.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace snt::ui {

// 2D position and size in pixels (top-left origin).
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    Vec2 pos;
    Vec2 size;
};

struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

// Texture mode is explicit in the draw contract. A retained MUI frame can
// contain solid primitives, signed-distance glyphs, pre-coloured glyphs, and
// registered UI images without selecting a compatibility rendering path.
enum class UiTextureMode : uint8_t {
    Solid = 0,
    SignedDistanceGlyph = 1,
    ColorGlyph = 2,
    Image = 3,
};

// Shared CPU-side raster payload for dynamic UI atlases. Producers mutate an
// atlas on the UI main thread; a submitted frame keeps a read-only shared
// reference while MuiRenderer uploads a revision before drawing it.
struct UiRasterAtlas {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
    uint64_t revision = 0;
};

// Dynamic glyph atlas shared by text layouts and the Vulkan MUI backend.
struct UiGlyphAtlas final : UiRasterAtlas {
    static constexpr uint32_t kDimension = 2048;

    UiGlyphAtlas() {
        width = kDimension;
        height = kDimension;
    }
};

// Dynamic image atlas shared by ImageView, SlotView, and future mod UI. It
// deliberately uses a separate texture from Unicode glyphs so content icon
// churn never invalidates shaped-text UVs.
struct UiImageAtlas final : UiRasterAtlas {
    static constexpr uint32_t kDimension = 2048;

    UiImageAtlas() {
        width = kDimension;
        height = kDimension;
    }
};

// A batch binds one atlas and one scissor state. Solid geometry uses the
// glyph binding because it does not sample a texture; image geometry selects
// the image binding. More atlas sources can be added without changing vertex
// layout or the retained-view API.
enum class UiTextureBinding : uint8_t {
    GlyphAtlas,
    ImageAtlas,
};

struct UiClipRect {
    bool enabled = false;
    Rect rect{};
};

// UI vertex: 2D position (pixels) + texture UV + RGBA color (0..255).
struct UiVertex {
    float position[2];  // x, y in pixels
    float uv[2];        // atlas UV, or negative for solid color
    uint8_t color[4];   // RGBA
    UiTextureMode texture_mode = UiTextureMode::Solid;
};

using UiIndex = uint32_t;

// One contiguous, painter-order-preserving indexed draw range. Vulkan maps
// `clip` to a dynamic scissor, keeping scroll and nested clip regions out of
// the fragment stage without duplicating geometry.
struct UiDrawBatch {
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    UiTextureBinding texture = UiTextureBinding::GlyphAtlas;
    UiClipRect clip{};
};

// Triangle-list draw data consumed by MuiRenderer. Index data is deliberately
// 32-bit: dense inventory/mod screens must not silently drop geometry once a
// frame crosses the old 16-bit vertex ceiling.
struct UiDrawData {
    std::vector<UiVertex> vertices;
    std::vector<UiIndex> indices;
    std::shared_ptr<const UiGlyphAtlas> glyph_atlas;
    std::shared_ptr<const UiImageAtlas> image_atlas;
    std::vector<UiDrawBatch> batches;
};

}  // namespace snt::ui
