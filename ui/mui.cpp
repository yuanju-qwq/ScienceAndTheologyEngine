// MUI implementation — text layout + draw list collection.
//
// MuiContext collects UiVertex/UiDrawData each frame. MuiRenderer consumes
// the draw data to submit Vulkan draw calls. Text layout uses the glyph
// table from MuiRenderer (baked via stb_truetype at init time).
//
// Widget stubs (button, checkbox, slider, plot) remain no-ops; they will
// be implemented when interactive UI is needed. Text rendering is the
// first real feature, enabling the debug overlay (TPS, FPS, etc.).

#include "mui.h"
#include "mui_renderer.h"  // GlyphInfo, MuiRenderer

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace snt::ui {

// ---------------------------------------------------------------------------
// MuiContext — frame lifecycle
// ---------------------------------------------------------------------------

void MuiContext::begin_frame() {
    draw_data_.vertices.clear();
    draw_data_.indices.clear();
    cursor_ = {0, 0};
}

void MuiContext::end_frame() {
    // Draw data is already collected during widget calls; nothing to do.
}

// ---------------------------------------------------------------------------
// MuiContext — windows
// ---------------------------------------------------------------------------

bool MuiContext::begin_window(std::string_view title, Rect rect) {
    (void)title;
    win_x_ = rect.pos.x;
    win_y_ = rect.pos.y;
    win_w_ = rect.size.x;
    // Start cursor at top-left interior padding.
    cursor_.x = win_x_ + padding_;
    cursor_.y = win_y_ + padding_;
    return true;
}

void MuiContext::end_window() {
    // No-op for now.
}

// ---------------------------------------------------------------------------
// MuiContext — widgets
// ---------------------------------------------------------------------------

void MuiContext::text(std::string_view fmt, ...) {
    // Format the string.
    va_list args;
    va_start(args, fmt);
    char buf[256];
    std::vsnprintf(buf, sizeof(buf), fmt.data(), args);
    va_end(args);

    // If no renderer is connected, text is a no-op (stub mode).
    if (!renderer_) return;

    // Layout each character as a quad (4 vertices + 6 indices).
    const float line_h = renderer_->line_height();
    float pen_x = cursor_.x;
    float pen_y = cursor_.y;

    for (const char* p = buf; *p != '\0'; ++p) {
        char c = *p;
        if (c == '\n') {
            pen_x = cursor_.x;
            pen_y += line_h;
            continue;
        }

        const GlyphInfo* g = renderer_->glyph(c);
        if (!g) continue;  // skip unsupported characters

        // Quad vertices (top-left origin, Y down).
        uint16_t base = static_cast<uint16_t>(draw_data_.vertices.size());
        UiVertex v;
        v.color[0] = 255; v.color[1] = 255; v.color[2] = 255; v.color[3] = 255;

        // Top-left.
        v.position[0] = pen_x + g->x0;
        v.position[1] = pen_y + g->y0;
        v.uv[0] = g->uv_x0; v.uv[1] = g->uv_y0;
        draw_data_.vertices.push_back(v);

        // Bottom-left.
        v.position[0] = pen_x + g->x0;
        v.position[1] = pen_y + g->y1;
        v.uv[0] = g->uv_x0; v.uv[1] = g->uv_y1;
        draw_data_.vertices.push_back(v);

        // Bottom-right.
        v.position[0] = pen_x + g->x1;
        v.position[1] = pen_y + g->y1;
        v.uv[0] = g->uv_x1; v.uv[1] = g->uv_y1;
        draw_data_.vertices.push_back(v);

        // Top-right.
        v.position[0] = pen_x + g->x1;
        v.position[1] = pen_y + g->y0;
        v.uv[0] = g->uv_x1; v.uv[1] = g->uv_y0;
        draw_data_.vertices.push_back(v);

        // Two triangles: (0,1,2) and (0,2,3).
        draw_data_.indices.push_back(base + 0);
        draw_data_.indices.push_back(base + 1);
        draw_data_.indices.push_back(base + 2);
        draw_data_.indices.push_back(base + 0);
        draw_data_.indices.push_back(base + 2);
        draw_data_.indices.push_back(base + 3);

        pen_x += g->advance;
    }

    // Advance cursor to the next line.
    cursor_.y += line_h;
}

void MuiContext::filled_rect(Rect rect, Color color) {
    if (rect.size.x <= 0.0f || rect.size.y <= 0.0f) {
        return;
    }
    if (draw_data_.vertices.size() + 4 > 0xFFFFu) {
        return;
    }

    uint16_t base = static_cast<uint16_t>(draw_data_.vertices.size());
    UiVertex v;
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

    v.position[0] = x0;
    v.position[1] = y0;
    draw_data_.vertices.push_back(v);

    v.position[0] = x0;
    v.position[1] = y1;
    draw_data_.vertices.push_back(v);

    v.position[0] = x1;
    v.position[1] = y1;
    draw_data_.vertices.push_back(v);

    v.position[0] = x1;
    v.position[1] = y0;
    draw_data_.vertices.push_back(v);

    draw_data_.indices.push_back(base + 0);
    draw_data_.indices.push_back(base + 1);
    draw_data_.indices.push_back(base + 2);
    draw_data_.indices.push_back(base + 0);
    draw_data_.indices.push_back(base + 2);
    draw_data_.indices.push_back(base + 3);
}

bool MuiContext::button(std::string_view label, Vec2 size) {
    (void)label;
    (void)size;
    return false;
}

bool MuiContext::checkbox(std::string_view label, bool& value) {
    (void)label;
    (void)value;
    return false;
}

bool MuiContext::slider_float(std::string_view label, float& value,
                              float min, float max) {
    (void)label;
    (void)value;
    (void)min;
    (void)max;
    return false;
}

bool MuiContext::slider_int(std::string_view label, int32_t& value,
                            int32_t min, int32_t max) {
    (void)label;
    (void)value;
    (void)min;
    (void)max;
    return false;
}

void MuiContext::plot_values(std::string_view label,
                             const float* values, int32_t count,
                             float min_value, float max_value) {
    (void)label;
    (void)values;
    (void)count;
    (void)min_value;
    (void)max_value;
}

// ---------------------------------------------------------------------------
// MuiContext — layout helpers
// ---------------------------------------------------------------------------

void MuiContext::spacing(float pixels) {
    cursor_.y += pixels;
}

void MuiContext::begin_group() {
    // No-op.
}

void MuiContext::end_group() {
    // No-op.
}

// ---------------------------------------------------------------------------
// Global default instance
// ---------------------------------------------------------------------------

MuiContext& default_mui_context() {
    static MuiContext instance;
    return instance;
}

}  // namespace snt::ui
