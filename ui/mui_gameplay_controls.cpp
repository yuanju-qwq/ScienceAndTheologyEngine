// Generic retained-MUI controls used by gameplay screens.

#define SNT_LOG_CHANNEL "mui.controls"
#include "ui/mui_gameplay_controls.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace snt::ui {
namespace {

[[nodiscard]] bool finite_positive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] bool is_ascii_whitespace(std::string_view value) noexcept {
    return value == " " || value == "\t" || value == "\r";
}

[[nodiscard]] bool contains_line_break(std::string_view value) noexcept {
    return value.find('\n') != std::string_view::npos;
}

struct TextFragment {
    std::string text;
    float advance = 0.0f;
    bool break_after = false;
};

[[nodiscard]] std::string join_fragments(const std::vector<TextFragment>& fragments,
                                         size_t begin, size_t end) {
    std::string value;
    for (size_t index = begin; index < end; ++index) value += fragments[index].text;
    return value;
}

[[nodiscard]] float fragments_width(const std::vector<TextFragment>& fragments,
                                    size_t begin, size_t end) noexcept {
    float width = 0.0f;
    for (size_t index = begin; index < end; ++index) width += fragments[index].advance;
    return width;
}

}  // namespace

ProgressBarView::ProgressBarView(std::string id)
    : View(std::move(id)) {}

void ProgressBarView::set_range(float minimum, float maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum) {
        SNT_LOG_WARN("ProgressBarView '%s' rejected invalid range %.3f..%.3f",
                     id().c_str(), minimum, maximum);
        return;
    }
    if (minimum_ == minimum && maximum_ == maximum) return;
    minimum_ = minimum;
    maximum_ = maximum;
    value_ = std::clamp(value_, minimum_, maximum_);
    mark_layout_dirty();
}

void ProgressBarView::set_value(float value) {
    if (!std::isfinite(value)) {
        SNT_LOG_WARN("ProgressBarView '%s' rejected a non-finite value", id().c_str());
        return;
    }
    const float clamped = std::clamp(value, minimum_, maximum_);
    if (value_ == clamped) return;
    value_ = clamped;
    mark_layout_dirty();
}

float ProgressBarView::normalized_value() const noexcept {
    const float span = maximum_ - minimum_;
    return span > 0.0f ? std::clamp((value_ - minimum_) / span, 0.0f, 1.0f) : 0.0f;
}

void ProgressBarView::measure(MeasureSpec width, MeasureSpec height, TextEngine&) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, 160.0f);
    measured_size_.y = resolve_axis(layout_params_.height, height, 16.0f);
}

void ProgressBarView::paint(Arc2DCommandBuffer& out,
                            TextEngine& text_engine,
                            const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible) return;

    constexpr float kInset = 1.0f;
    constexpr float kRadius = 3.0f;
    const Rect track{
        .pos = {bounds_.pos.x + kInset, bounds_.pos.y + kInset},
        .size = {std::max(0.0f, bounds_.size.x - 2.0f * kInset),
                 std::max(0.0f, bounds_.size.y - 2.0f * kInset)},
    };
    out.rect(track, track_color_, kRadius);
    const float filled = track.size.x * normalized_value();
    if (filled > 0.0f) {
        out.rect({.pos = track.pos, .size = {filled, track.size.y}}, fill_color_, kRadius);
    }
}

WrappedTextView::WrappedTextView(std::string id)
    : TextView(std::move(id)) {}

void WrappedTextView::set_line_spacing(float value) {
    if (!finite_positive(value)) {
        SNT_LOG_WARN("WrappedTextView '%s' rejected invalid line spacing %.3f",
                     id().c_str(), value);
        return;
    }
    if (line_spacing_ == value) return;
    line_spacing_ = value;
    mark_layout_dirty();
}

float WrappedTextView::line_height() const noexcept {
    return std::max(1.0f, style_.size_px * line_spacing_);
}

void WrappedTextView::rebuild_lines(TextEngine& text_engine, float available_width) {
    lines_.clear();
    const float safe_width = std::max(1.0f, available_width);
    const TextLayout source = text_engine.shape(text_, style_);
    std::vector<TextFragment> current;
    float current_width = 0.0f;

    const auto flush_line = [&]() {
        while (!current.empty() && is_ascii_whitespace(current.back().text)) current.pop_back();
        if (current.empty()) {
            lines_.push_back({.text = {}, .layout = text_engine.shape({}, style_)});
        } else {
            std::string text = join_fragments(current, 0, current.size());
            TextLayout layout = text_engine.shape(text, style_);
            lines_.push_back({.text = std::move(text), .layout = std::move(layout)});
        }
        current.clear();
        current_width = 0.0f;
    };

    const auto append_fragment = [&](TextFragment fragment) {
        current_width += fragment.advance;
        current.push_back(std::move(fragment));
    };

    for (const TextCluster& cluster : source.clusters) {
        if (contains_line_break(cluster.utf8)) {
            flush_line();
            continue;
        }
        TextFragment fragment{
            .text = cluster.utf8,
            .advance = std::max(0.0f, cluster.advance),
            .break_after = is_ascii_whitespace(cluster.utf8),
        };

        while (!current.empty() && current_width + fragment.advance > safe_width) {
            size_t break_index = current.size();
            for (size_t index = current.size(); index > 0; --index) {
                if (current[index - 1].break_after) {
                    break_index = index;
                    break;
                }
            }
            if (break_index == current.size()) {
                flush_line();
                break;
            }

            std::vector<TextFragment> overflow;
            overflow.reserve(current.size() - break_index);
            for (size_t index = break_index; index < current.size(); ++index) {
                if (!overflow.empty() || !is_ascii_whitespace(current[index].text)) {
                    overflow.push_back(std::move(current[index]));
                }
            }
            current.resize(break_index);
            flush_line();
            current = std::move(overflow);
            current_width = fragments_width(current, 0, current.size());
        }
        append_fragment(std::move(fragment));
    }
    if (!current.empty() || text_.empty()) flush_line();

    if (max_lines_ == 0 || lines_.size() <= max_lines_) return;
    lines_.resize(max_lines_);
    Line& last = lines_.back();
    const TextLayout ellipsis_layout = text_engine.shape("...", style_);
    const float ellipsis_width = ellipsis_layout.size.x;
    const TextLayout current_layout = text_engine.shape(last.text, style_);
    std::string truncated;
    float consumed = 0.0f;
    for (const TextCluster& cluster : current_layout.clusters) {
        if (consumed + cluster.advance + ellipsis_width > safe_width) break;
        truncated += cluster.utf8;
        consumed += cluster.advance;
    }
    last.text = std::move(truncated) + "...";
    last.layout = text_engine.shape(last.text, style_);
}

void WrappedTextView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        lines_.clear();
        return;
    }
    const float available_width = width.mode == MeasureMode::Unspecified
        ? std::numeric_limits<float>::max() * 0.25f
        : std::max(1.0f, width.size);
    rebuild_lines(text_engine, available_width);

    float desired_width = 0.0f;
    for (const Line& line : lines_) desired_width = std::max(desired_width, line.layout.size.x);
    measured_size_.x = resolve_axis(layout_params_.width, width, desired_width);
    measured_size_.y = resolve_axis(layout_params_.height, height,
                                    line_height() * static_cast<float>(lines_.size()));
}

void WrappedTextView::paint(Arc2DCommandBuffer& out,
                            TextEngine& text_engine,
                            const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible) return;

    const float height = line_height();
    for (size_t index = 0; index < lines_.size(); ++index) {
        const float y = bounds_.pos.y + static_cast<float>(index) * height;
        if (y >= bounds_.pos.y + bounds_.size.y) break;
        out.text({.pos = {bounds_.pos.x, y}, .size = {bounds_.size.x, height}},
                 lines_[index].text, style_, lines_[index].layout);
    }
}

PanZoomView::PanZoomView(std::string id)
    : ViewGroup(std::move(id)) {
    set_hit_test_visible(true);
    set_focusable(true);
}

void PanZoomView::set_world_size(Vec2 size) {
    if (!finite_positive(size.x) || !finite_positive(size.y)) {
        SNT_LOG_WARN("PanZoomView '%s' rejected invalid world size %.2fx%.2f",
                     id().c_str(), size.x, size.y);
        return;
    }
    if (world_size_.x == size.x && world_size_.y == size.y) return;
    world_size_ = size;
    clamp_world_origin();
    mark_layout_dirty();
}

void PanZoomView::set_zoom_range(float minimum, float maximum) {
    if (!finite_positive(minimum) || !finite_positive(maximum) || maximum < minimum) {
        SNT_LOG_WARN("PanZoomView '%s' rejected invalid zoom range %.3f..%.3f",
                     id().c_str(), minimum, maximum);
        return;
    }
    minimum_zoom_ = minimum;
    maximum_zoom_ = maximum;
    set_zoom(zoom_);
}

void PanZoomView::set_zoom(float zoom) {
    if (!finite_positive(zoom)) {
        SNT_LOG_WARN("PanZoomView '%s' rejected invalid zoom %.3f", id().c_str(), zoom);
        return;
    }
    const float clamped = std::clamp(zoom, minimum_zoom_, maximum_zoom_);
    if (zoom_ == clamped) return;
    zoom_ = clamped;
    clamp_world_origin();
    mark_layout_dirty();
}

void PanZoomView::set_world_origin(Vec2 origin) {
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y)) {
        SNT_LOG_WARN("PanZoomView '%s' rejected a non-finite world origin", id().c_str());
        return;
    }
    if (world_origin_.x == origin.x && world_origin_.y == origin.y) return;
    world_origin_ = origin;
    clamp_world_origin();
    mark_layout_dirty();
}

void PanZoomView::center_on(Vec2 world_point) {
    set_world_origin({world_point.x - bounds_.size.x / (2.0f * zoom_),
                      world_point.y - bounds_.size.y / (2.0f * zoom_)});
}

Vec2 PanZoomView::world_to_screen(Vec2 point) const noexcept {
    return {bounds_.pos.x + (point.x - world_origin_.x) * zoom_,
            bounds_.pos.y + (point.y - world_origin_.y) * zoom_};
}

Vec2 PanZoomView::screen_to_world(Vec2 point) const noexcept {
    return {world_origin_.x + (point.x - bounds_.pos.x) / zoom_,
            world_origin_.y + (point.y - bounds_.pos.y) / zoom_};
}

void PanZoomView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    for (auto& child : children_) {
        child->measure({.size = world_size_.x, .mode = MeasureMode::AtMost},
                       {.size = world_size_.y, .mode = MeasureMode::AtMost}, text_engine);
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, world_size_.x);
    measured_size_.y = resolve_axis(layout_params_.height, height, world_size_.y);
}

void PanZoomView::layout(Rect bounds) {
    View::layout(bounds);
    clamp_world_origin();
    for (auto& child : children_) {
        const LayoutParams& params = child->layout_params();
        const Vec2 position = world_to_screen({params.margin.left, params.margin.top});
        child->layout({.pos = position,
                       .size = {child->measured_size().x * zoom_,
                                child->measured_size().y * zoom_}});
    }
}

void PanZoomView::paint(Arc2DCommandBuffer& out,
                        TextEngine& text_engine,
                        const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible) return;
    out.push_clip(bounds_);
    for (const auto& child : children_) child->paint(out, text_engine, theme);
    out.pop_clip();
}

UiEventReply PanZoomView::on_input_event(const UiInputEvent& event) {
    const UiEventReply base_reply = View::on_input_event(event);
    if (base_reply == UiEventReply::StopPropagation || !enabled() ||
        visibility() != Visibility::Visible) {
        return base_reply;
    }

    if (event.type == UiInputEventType::PointerScroll &&
        (event.phase == UiEventPhase::Target || event.phase == UiEventPhase::Bubble) &&
        event.scroll_delta.y != 0.0f) {
        const float steps = std::clamp(event.scroll_delta.y, -4.0f, 4.0f);
        set_zoom_around(zoom_ * std::pow(1.15f, steps), event.pointer_position);
        return UiEventReply::StopPropagation;
    }
    if (event.phase != UiEventPhase::Target) return base_reply;

    if (event.type == UiInputEventType::PointerDown &&
        (event.pointer_button == UiPointerButton::Primary ||
         event.pointer_button == UiPointerButton::Middle)) {
        panning_ = true;
        pan_button_ = event.pointer_button;
        last_pointer_position_ = event.pointer_position;
        return UiEventReply::Handled;
    }
    if (event.type == UiInputEventType::PointerMove && panning_) {
        const Vec2 delta{event.pointer_position.x - last_pointer_position_.x,
                         event.pointer_position.y - last_pointer_position_.y};
        last_pointer_position_ = event.pointer_position;
        set_world_origin({world_origin_.x - delta.x / zoom_,
                          world_origin_.y - delta.y / zoom_});
        return UiEventReply::Handled;
    }
    if (event.type == UiInputEventType::PointerUp && panning_ &&
        event.pointer_button == pan_button_) {
        panning_ = false;
        pan_button_ = UiPointerButton::None;
        return UiEventReply::Handled;
    }
    return base_reply;
}

bool PanZoomView::accepts_child_input(Vec2 point) const {
    return point.x >= bounds_.pos.x && point.y >= bounds_.pos.y &&
           point.x < bounds_.pos.x + bounds_.size.x &&
           point.y < bounds_.pos.y + bounds_.size.y;
}

void PanZoomView::set_zoom_around(float zoom, Vec2 screen_anchor) {
    if (!finite_positive(zoom)) return;
    const Vec2 anchor_world = screen_to_world(screen_anchor);
    const float clamped = std::clamp(zoom, minimum_zoom_, maximum_zoom_);
    if (zoom_ == clamped) return;
    zoom_ = clamped;
    world_origin_ = {anchor_world.x - (screen_anchor.x - bounds_.pos.x) / zoom_,
                     anchor_world.y - (screen_anchor.y - bounds_.pos.y) / zoom_};
    clamp_world_origin();
    mark_layout_dirty();
}

void PanZoomView::clamp_world_origin() {
    if (bounds_.size.x <= 0.0f || bounds_.size.y <= 0.0f || zoom_ <= 0.0f) return;
    const float max_x = std::max(0.0f, world_size_.x - bounds_.size.x / zoom_);
    const float max_y = std::max(0.0f, world_size_.y - bounds_.size.y / zoom_);
    world_origin_.x = std::clamp(world_origin_.x, 0.0f, max_x);
    world_origin_.y = std::clamp(world_origin_.y, 0.0f, max_y);
}

}  // namespace snt::ui
