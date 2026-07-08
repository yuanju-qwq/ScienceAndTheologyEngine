// Debug Panel — P1 stub implementation.

#include "debug_panel.h"
#include "mui.h"

#include <algorithm>
#include <cstring>

namespace snt::ui {

// ---------------------------------------------------------------------------
// DebugPanel
// ---------------------------------------------------------------------------

void DebugPanel::register_metric(std::string_view name, MetricGetter getter) {
    // Avoid duplicate names.
    for (const auto& m : metrics_) {
        if (m.name == name) return;
    }
    metrics_.push_back(Metric{
        .name = std::string(name),
        .getter = std::move(getter),
    });
}

void DebugPanel::sample() {
    for (auto& m : metrics_) {
        m.current_value = m.getter ? m.getter() : 0.0f;
        m.history[m.history_offset] = m.current_value;
        m.history_offset = (m.history_offset + 1) % Metric::kHistorySize;
    }
}

void DebugPanel::draw() {
    if (!visible_) return;

    MuiContext& ctx = default_mui_context();

    // P1 stub: begin/end window with no visible widgets.
    // P1.4 will: real layout, FPS plot, memory bars, etc.
    if (ctx.begin_window("Debug", {.pos = {10, 10}, .size = {300, 200}})) {
        for (const auto& m : metrics_) {
            ctx.text("%s: %.2f", m.name.c_str(), m.current_value);
        }
        ctx.end_window();
    }
}

// ---------------------------------------------------------------------------
// Global default instance
// ---------------------------------------------------------------------------

DebugPanel& default_debug_panel() {
    static DebugPanel instance;
    return instance;
}

}  // namespace snt::ui
