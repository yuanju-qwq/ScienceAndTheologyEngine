// MUI — Minimal Immediate-mode UI.
//
// Self-developed immediate-mode GUI, inspired by Dear ImGui and Minecraft
// Modern UI (MUI) mod's self-rendered approach.
//
// Design goals:
//   - Immediate-mode: no retained widget tree; UI is re-issued every frame.
//   - Thin abstraction over the render backend (Vulkan draw calls).
//   - Suitable for debug overlays, inspector panels, and in-game tools.
//   - P1: API design only; rendering is a stub (no-op draws).
//   - P1.4: real implementation backed by Vulkan vertex buffer + shader.
//
// Usage example (debug overlay):
//   MuiContext ctx;
//   ctx.begin_frame();
//   if (ctx.begin_window("Debug", {10, 10, 300, 200})) {
//       ctx.text("FPS: %.1f", fps);
//       if (ctx.button("Reload")) { reload_scripts(); }
//       ctx.plot_values("Frame Time", frame_times, 120);
//       ctx.end_window();
//   }
//   ctx.end_frame();

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace snt::ui {

// Forward declaration: MuiRenderer provides glyph lookup for text layout.
// The full definition lives in mui_renderer.h (which includes Vulkan
// headers); mui.cpp includes it, but mui.h itself stays Vulkan-free.
class MuiRenderer;

// 2D position and size (float for sub-pixel layout / DPI scaling).
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    Vec2 pos;
    Vec2 size;
};

// UI vertex: 2D position (pixels) + texture UV + RGBA color (0..255).
// Defined here (not in mui_renderer.h) so MuiContext can build draw
// lists without pulling in Vulkan headers.
struct UiVertex {
    float    position[2];  // x, y in pixels (top-left origin)
    float    uv[2];        // font atlas UV
    uint8_t  color[4];     // RGBA (0..255)
};

// Draw data produced by MuiContext::end_frame(). Consumed by MuiRenderer.
struct UiDrawData {
    std::vector<UiVertex>  vertices;
    std::vector<uint16_t>  indices;  // triangle list
};

// Font atlas glyph info (one per baked character).
struct GlyphInfo {
    float uv_x0, uv_y0;  // top-left UV in atlas
    float uv_x1, uv_y1;  // bottom-right UV in atlas
    float x0, y0, x1, y1; // vertex offsets relative to pen position
    float advance;        // pen advance (pixels)
};

// MUI context: holds the current UI state for one frame.
// P1 stub: all draw calls are no-ops; the API is fixed here so that
// gameplay code can be written against it before the renderer is ready.
class MuiContext {
public:
    MuiContext() = default;
    ~MuiContext() = default;

    // Non-copyable (internal state).
    MuiContext(const MuiContext&) = delete;
    MuiContext& operator=(const MuiContext&) = delete;

    // Connect a MuiRenderer for glyph lookup. Must be called before
    // begin_frame() if text rendering is desired. Pass nullptr to
    // disable text output (reverts to stub behavior).
    void set_renderer(MuiRenderer* renderer) { renderer_ = renderer; }

    // ---- Frame lifecycle ----

    // Begin a new UI frame. Resets draw list and layout cursor.
    void begin_frame();

    // End the current frame. Finalizes the draw list (no-op for now;
    // data is already collected during widget calls).
    void end_frame();

    // Access the collected draw data for this frame. The RenderSystem
    // reads this after end_frame() to submit UI draws.
    const UiDrawData& draw_data() const { return draw_data_; }

    // ---- Windows ----

    // Begin a floating window. Returns true if the window is visible
    // (not collapsed). Always pair with end_window().
    bool begin_window(std::string_view title, Rect rect);

    // End the current window.
    void end_window();

    // ---- Widgets ----

    // Formatted text label.
    void text(std::string_view fmt, ...);

    // Button. Returns true if clicked this frame.
    bool button(std::string_view label, Vec2 size = {0, 0});

    // Checkbox. `value` is read and written in place.
    bool checkbox(std::string_view label, bool& value);

    // Float slider. Returns true if `value` changed this frame.
    bool slider_float(std::string_view label, float& value,
                      float min, float max);

    // Integer slider. Returns true if `value` changed this frame.
    bool slider_int(std::string_view label, int32_t& value,
                    int32_t min, int32_t max);

    // Plot a line graph of `values` (e.g. frame times, entity counts).
    void plot_values(std::string_view label,
                     const float* values, int32_t count,
                     float min_value = 0.0f, float max_value = 0.0f);

    // ---- Layout helpers ----

    // Add spacing between widgets.
    void spacing(float pixels = 8.0f);

    // Begin a horizontally-laid-out group. Pair with end_group().
    void begin_group();
    void end_group();

private:
    MuiRenderer*  renderer_ = nullptr;   // glyph lookup (may be null)
    UiDrawData    draw_data_;            // per-frame vertex/index lists
    Vec2          cursor_   = {0, 0};    // current text pen position
    float         win_x_    = 0.0f;      // current window left edge
    float         win_y_    = 0.0f;      // current window top edge
    float         win_w_    = 0.0f;      // current window width
    float         padding_  = 8.0f;      // interior padding
};

// Global default MUI context (used by debug panels and gameplay code).
MuiContext& default_mui_context();

}  // namespace snt::ui
