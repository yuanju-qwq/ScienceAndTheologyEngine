// UI draw data shared by retained MUI and the Vulkan UI renderer.
//
// This is the engine-native draw contract: retained views emit Arc2D
// commands, Arc2D lowers supported primitives to this vertex/index stream,
// and MuiRenderer records it into the current Vulkan command buffer.

#pragma once

#include <cstdint>
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

// UI vertex: 2D position (pixels) + texture UV + RGBA color (0..255).
struct UiVertex {
    float    position[2];  // x, y in pixels
    float    uv[2];        // atlas UV, or negative for solid color
    uint8_t  color[4];     // RGBA
};

// Triangle-list draw data consumed by MuiRenderer.
struct UiDrawData {
    std::vector<UiVertex> vertices;
    std::vector<uint16_t> indices;
};

}  // namespace snt::ui
