// Reusable retained-MUI controls for dense gameplay screens.
//
// These widgets intentionally carry no game/content semantics. Game modules
// provide node data, commands, and visual policy; this module owns only the
// viewport transform, progress presentation, and Unicode-aware text layout.

#pragma once

#include "ui/retained_mui_view.h"

#include <cstddef>
#include <vector>

namespace snt::ui {

// Determinate value presentation for task objectives, loading, and resource
// meters. Callers render any accompanying labels separately so the control
// remains useful in compact HUDs as well as detailed panels.
class ProgressBarView final : public View {
public:
    explicit ProgressBarView(std::string id = {});
    ViewKind kind() const override { return ViewKind::ProgressBar; }

    void set_range(float minimum, float maximum);
    [[nodiscard]] float minimum() const noexcept { return minimum_; }
    [[nodiscard]] float maximum() const noexcept { return maximum_; }
    void set_value(float value);
    [[nodiscard]] float value() const noexcept { return value_; }
    [[nodiscard]] float normalized_value() const noexcept;
    void set_track_color(Color color) noexcept { track_color_ = color; }
    void set_fill_color(Color color) noexcept { fill_color_ = color; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;

private:
    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    float value_ = 0.0f;
    Color track_color_{30, 39, 48, 255};
    Color fill_color_{74, 174, 112, 255};
};

// TextView variant that wraps at Unicode shaping-cluster boundaries. It does
// not interpret markup, so untrusted task text cannot alter the renderer.
class WrappedTextView : public TextView {
public:
    explicit WrappedTextView(std::string id = {});
    ViewKind kind() const override { return ViewKind::WrappedText; }

    void set_max_lines(size_t max_lines) {
        if (max_lines_ == max_lines) return;
        max_lines_ = max_lines;
        mark_layout_dirty();
    }
    [[nodiscard]] size_t max_lines() const noexcept { return max_lines_; }
    void set_line_spacing(float value);
    [[nodiscard]] float line_spacing() const noexcept { return line_spacing_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;

private:
    struct Line {
        std::string text;
        TextLayout layout;
    };

    void rebuild_lines(TextEngine& text_engine, float available_width);
    [[nodiscard]] float line_height() const noexcept;

    std::vector<Line> lines_;
    float line_spacing_ = 1.25f;
    size_t max_lines_ = 0;
};

// A clipped finite world viewport for node graphs, maps, and relationship
// diagrams. Child layout margins are world coordinates; child sizes are world
// units and are transformed consistently with the viewport zoom. Primary
// drag pans empty canvas space, while wheel input zooms around the pointer.
class PanZoomView final : public ViewGroup {
public:
    explicit PanZoomView(std::string id = {});
    ViewKind kind() const override { return ViewKind::PanZoom; }

    void set_world_size(Vec2 size);
    [[nodiscard]] Vec2 world_size() const noexcept { return world_size_; }
    void set_zoom_range(float minimum, float maximum);
    [[nodiscard]] float minimum_zoom() const noexcept { return minimum_zoom_; }
    [[nodiscard]] float maximum_zoom() const noexcept { return maximum_zoom_; }
    void set_zoom(float zoom);
    [[nodiscard]] float zoom() const noexcept { return zoom_; }
    void set_world_origin(Vec2 origin);
    [[nodiscard]] Vec2 world_origin() const noexcept { return world_origin_; }
    void center_on(Vec2 world_point);
    [[nodiscard]] Vec2 world_to_screen(Vec2 point) const noexcept;
    [[nodiscard]] Vec2 screen_to_world(Vec2 point) const noexcept;

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;
    bool accepts_child_input(Vec2 point) const override;

private:
    void set_zoom_around(float zoom, Vec2 screen_anchor);
    void clamp_world_origin();

    Vec2 world_size_{1.0f, 1.0f};
    Vec2 world_origin_{};
    float minimum_zoom_ = 0.45f;
    float maximum_zoom_ = 2.5f;
    float zoom_ = 1.0f;
    bool panning_ = false;
    UiPointerButton pan_button_ = UiPointerButton::None;
    Vec2 last_pointer_position_{};
};

}  // namespace snt::ui
