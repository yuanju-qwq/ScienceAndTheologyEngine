// MUI — P1 stub implementation.
//
// All draw calls and input handling are no-ops. This fixes the API so that
// gameplay/debug code can be written against it before the Vulkan renderer
// is ready (P1.4). The stub always returns "no interaction" from widgets.

#include "mui.h"

#include <cstdarg>
#include <cstdio>

namespace snt::ui {

// ---------------------------------------------------------------------------
// MuiContext — frame lifecycle
// ---------------------------------------------------------------------------

void MuiContext::begin_frame() {
    // P1 stub: no state to set up. P1.4 will capture input + reset draw list.
}

void MuiContext::end_frame() {
    // P1 stub: no draw calls to submit. P1.4 will record vertices and
    // hand them to the Vulkan render backend.
}

// ---------------------------------------------------------------------------
// MuiContext — windows
// ---------------------------------------------------------------------------

bool MuiContext::begin_window(std::string_view title, Rect rect) {
    // P1 stub: always returns true (window visible).
    // P1.4 will: push window onto stack, handle move/resize, collapse state.
    (void)title;
    (void)rect;
    return true;
}

void MuiContext::end_window() {
    // P1 stub: no-op.
}

// ---------------------------------------------------------------------------
// MuiContext — widgets
// ---------------------------------------------------------------------------

void MuiContext::text(std::string_view fmt, ...) {
    // P1 stub: format string to nowhere (just validates the call).
    // P1.4 will: lay out text, push glyphs to draw list.
    va_list args;
    va_start(args, fmt);
    char buf[256];
    std::vsnprintf(buf, sizeof(buf), fmt.data(), args);
    va_end(args);
}

bool MuiContext::button(std::string_view label, Vec2 size) {
    // P1 stub: never clicked.
    (void)label;
    (void)size;
    return false;
}

bool MuiContext::checkbox(std::string_view label, bool& value) {
    // P1 stub: no toggle.
    (void)label;
    (void)value;
    return false;
}

bool MuiContext::slider_float(std::string_view label, float& value,
                              float min, float max) {
    // P1 stub: value unchanged.
    (void)label;
    (void)value;
    (void)min;
    (void)max;
    return false;
}

bool MuiContext::slider_int(std::string_view label, int32_t& value,
                            int32_t min, int32_t max) {
    // P1 stub: value unchanged.
    (void)label;
    (void)value;
    (void)min;
    (void)max;
    return false;
}

void MuiContext::plot_values(std::string_view label,
                             const float* values, int32_t count,
                             float min_value, float max_value) {
    // P1 stub: no-op.
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
    // P1 stub: no-op.
    (void)pixels;
}

void MuiContext::begin_group() {
    // P1 stub: no-op.
}

void MuiContext::end_group() {
    // P1 stub: no-op.
}

// ---------------------------------------------------------------------------
// Global default instance
// ---------------------------------------------------------------------------

MuiContext& default_mui_context() {
    static MuiContext instance;
    return instance;
}

}  // namespace snt::ui
