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
#include <string_view>

namespace snt::ui {

// 2D position and size (float for sub-pixel layout / DPI scaling).
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    Vec2 pos;
    Vec2 size;
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

    // ---- Frame lifecycle ----

    // Begin a new UI frame. Captures input state, clears draw list.
    void begin_frame();

    // End the current frame. Submits recorded draw calls to the render
    // backend (P1 stub: no-op).
    void end_frame();

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
    // P1 stub: no internal state needed (all draws are no-ops).
    // P1.4 will add: draw list (vertices/indices), input state,
    // layout cursor, focus stack, etc.
};

// Global default MUI context (used by debug panels and gameplay code).
MuiContext& default_mui_context();

}  // namespace snt::ui
