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
// contain solid primitives, signed-distance glyphs, and pre-coloured glyphs
// (for example emoji) without selecting a compatibility rendering path.
enum class UiTextureMode : uint8_t {
    Solid = 0,
    SignedDistanceGlyph = 1,
    ColorGlyph = 2,
};

// CPU-side dynamic glyph atlas shared by text layouts and the Vulkan MUI
// backend. UnicodeTextEngine owns mutation; frames only keep a shared,
// read-only reference and MuiRenderer uploads a new revision before drawing.
struct UiGlyphAtlas {
    static constexpr uint32_t kDimension = 2048;

    uint32_t width = kDimension;
    uint32_t height = kDimension;
    std::vector<uint8_t> rgba;
    uint64_t revision = 0;
};

// UI vertex: 2D position (pixels) + texture UV + RGBA color (0..255).
struct UiVertex {
    float position[2];  // x, y in pixels
    float uv[2];        // atlas UV, or negative for solid color
    uint8_t color[4];   // RGBA
    UiTextureMode texture_mode = UiTextureMode::Solid;
};

// Triangle-list draw data consumed by MuiRenderer.
struct UiDrawData {
    std::vector<UiVertex> vertices;
    std::vector<uint16_t> indices;
    std::shared_ptr<const UiGlyphAtlas> glyph_atlas;
};

}  // namespace snt::ui
