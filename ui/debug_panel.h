// Debug Panel — engine-level debug overlay using MUI.
//
// Registers engine metrics (FPS, frame time, GPU memory, ECS entity count,
// job system queue depth) and draws them in an overlay window.
//
// P1: metric registration + stub draw (no visible output).
// P1.4: real draw via MUI + Vulkan backend.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace snt::ui {

// A metric is a named value source. The getter returns the current value
// as a float (e.g. FPS, memory in MB, entity count).
using MetricGetter = std::function<float()>;

// A text metric is a named string source. Used for metrics whose display
// format combines multiple values (e.g. "TPS: 20.0 (12.3mspt)" where the
// number and the parenthetical both come from engine state). Text metrics
// are drawn verbatim and do not participate in plotting.
using TextMetricGetter = std::function<std::string()>;

struct Metric {
    std::string name;
    MetricGetter getter;
    float current_value = 0.0f;
    // Ring buffer of recent samples for plotting.
    static constexpr int32_t kHistorySize = 120;
    float history[kHistorySize] = {};
    int32_t history_offset = 0;
};

struct TextMetric {
    std::string name;
    TextMetricGetter getter;
    std::string current_value;
};

// Debug panel: collects metrics and draws them via MUIContext.
class DebugPanel {
public:
    DebugPanel() = default;
    ~DebugPanel() = default;

    // Non-copyable.
    DebugPanel(const DebugPanel&) = delete;
    DebugPanel& operator=(const DebugPanel&) = delete;

    // Register a metric. `name` must be unique.
    void register_metric(std::string_view name, MetricGetter getter);

    // Register a text metric (string-valued, drawn verbatim). `name` must
    // be unique across BOTH metric types.
    void register_text_metric(std::string_view name, TextMetricGetter getter);

    // Sample all registered metrics (call once per frame, before draw).
    void sample();

    // Draw the panel via the default MUI context.
    // P1 stub: no visible output. P1.4: real draw.
    void draw();

    // Toggle visibility (e.g. bound to F1 key).
    void toggle_visible() { visible_ = !visible_; }
    bool is_visible() const { return visible_; }

private:
    std::vector<Metric>     metrics_;
    std::vector<TextMetric> text_metrics_;
    bool visible_ = true;
};

// Global default debug panel (engine-level metrics registered by modules).
DebugPanel& default_debug_panel();

}  // namespace snt::ui
