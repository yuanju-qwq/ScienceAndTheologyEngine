#define SNT_LOG_CHANNEL "ui"
#include "retained_mui_runtime.h"

#include "core/log.h"

#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

namespace snt::ui {

UiRuntime::UiRuntime(const snt::core::RuntimePathResolver& paths,
                     TextEngineConfig config,
                     UiTheme theme)
    : text_engine_(paths, std::move(config)),
      images_(),
      renderer_(images_),
      theme_(std::move(theme)) {
    layers_.set_retained_root_invalidator(
        [this](View& root) { input_router_.cancel_interaction_for_root(root); });
}

void UiRuntime::set_viewport(UiViewport viewport) {
    if (!viewport.valid()) {
        SNT_LOG_WARN("MUI ignored invalid viewport metrics");
        return;
    }
    const bool changed = !viewport_.valid() ||
        viewport_.framebuffer_size.x != viewport.framebuffer_size.x ||
        viewport_.framebuffer_size.y != viewport.framebuffer_size.y ||
        viewport_.window_size.x != viewport.window_size.x ||
        viewport_.window_size.y != viewport.window_size.y ||
        viewport_.dpi_scale != viewport.dpi_scale ||
        viewport_.user_scale != viewport.user_scale;
    viewport_ = viewport;
    if (changed) {
        const Vec2 logical = viewport_.logical_size();
        SNT_LOG_INFO("MUI viewport changed: framebuffer=%.0fx%.0f window=%.0fx%.0f "
                     "dpi=%.2f user=%.2f logical=%.1fx%.1f",
                     viewport_.framebuffer_size.x, viewport_.framebuffer_size.y,
                     viewport_.window_size.x, viewport_.window_size.y,
                     viewport_.dpi_scale, viewport_.user_scale, logical.x, logical.y);
    }
}

void UiRuntime::set_user_scale(float scale) {
    if (!std::isfinite(scale) || scale <= 0.0f) {
        SNT_LOG_WARN("MUI ignored invalid user scale %.3f", scale);
        return;
    }
    UiViewport next = viewport_;
    if (!next.valid()) {
        next.framebuffer_size = {1.0f, 1.0f};
        next.window_size = {1.0f, 1.0f};
        next.dpi_scale = 1.0f;
    }
    next.user_scale = scale;
    set_viewport(next);
}

void UiRuntime::set_clipboard(std::shared_ptr<IUiClipboard> clipboard) {
    text_input_service_.set_clipboard(std::move(clipboard));
}

void UiRuntime::set_text_input_platform(std::shared_ptr<IUiTextInputPlatform> platform) {
    text_input_service_.set_text_input_platform(std::move(platform));
}

void UiRuntime::layout(View& root, Vec2 viewport) {
    if (!viewport_.valid()) {
        set_viewport({.framebuffer_size = viewport, .window_size = viewport});
    }
    const bool viewport_changed = !root.has_layout_viewport_ ||
        root.last_layout_viewport_.x != viewport.x || root.last_layout_viewport_.y != viewport.y;
    if (!root.layout_dirty_ && !viewport_changed) return;

    root.measure({.size = viewport.x, .mode = MeasureMode::Exactly},
                 {.size = viewport.y, .mode = MeasureMode::Exactly},
                 text_engine_);
    root.layout({.pos = {0.0f, 0.0f}, .size = viewport});

    const auto clear_dirty = [](auto&& self, View& view) -> void {
        view.layout_dirty_ = false;
        if (auto* group = dynamic_cast<ViewGroup*>(&view)) {
            for (auto& child : group->children()) self(self, *child);
        }
    };
    clear_dirty(clear_dirty, root);
    root.last_layout_viewport_ = viewport;
    root.has_layout_viewport_ = true;
}

void UiRuntime::begin_input_frame(UiInputState input, std::span<View*> active_roots) {
    input_router_.begin_frame(std::move(input), active_roots);
}

bool UiRuntime::dispatch_pointer_input(View& root) {
    return input_router_.dispatch_pointer_input(root);
}

bool UiRuntime::dispatch_keyboard_input(View& root) {
    return input_router_.dispatch_keyboard_input(root, text_input_service_);
}

void UiRuntime::end_input_frame(std::span<View*> active_roots) {
    input_router_.end_frame(active_roots);
}

void UiRuntime::synchronize_interaction_state(View& root) {
    input_router_.synchronize_interaction_state(root);
}

std::optional<Rect> UiRuntime::focused_text_input_bounds(const View& root) const {
    return input_router_.focused_text_input_bounds(root);
}

void UiRuntime::synchronize_text_input_platform(std::span<View*> active_roots, bool enabled) {
    std::optional<Rect> bounds;
    std::string_view root_id;
    std::string_view view_id;
    if (enabled && viewport_.valid()) {
        if (const auto target = input_router_.focused_text_input(active_roots)) {
            bounds = target->bounds;
            root_id = target->root_id;
            view_id = target->view_id;
        }
    }
    text_input_service_.synchronize_platform(viewport_, bounds, root_id, view_id, enabled);
}

UiFrameResult UiRuntime::paint(View& root) {
    UiFrameResult result;
    root.paint(result.commands, text_engine_, theme_);
    result.draw_data = renderer_.build_draw_data(result.commands);
    const float scale = viewport_.valid() ? viewport_.pixels_per_ui_unit() : 1.0f;
    if (scale != 1.0f) {
        for (UiVertex& vertex : result.draw_data.vertices) {
            vertex.position[0] *= scale;
            vertex.position[1] *= scale;
        }
        for (UiDrawBatch& batch : result.draw_data.batches) {
            if (!batch.clip.enabled) continue;
            batch.clip.rect.pos.x *= scale;
            batch.clip.rect.pos.y *= scale;
            batch.clip.rect.size.x *= scale;
            batch.clip.rect.size.y *= scale;
        }
    }
    return result;
}

UiDrawData UiRuntime::build_draw_data(const Arc2DCommandBuffer& commands) {
    UiDrawData data = renderer_.build_draw_data(commands);
    const float scale = viewport_.valid() ? viewport_.pixels_per_ui_unit() : 1.0f;
    if (scale == 1.0f) return data;
    for (UiVertex& vertex : data.vertices) {
        vertex.position[0] *= scale;
        vertex.position[1] *= scale;
    }
    for (UiDrawBatch& batch : data.batches) {
        if (!batch.clip.enabled) continue;
        batch.clip.rect.pos.x *= scale;
        batch.clip.rect.pos.y *= scale;
        batch.clip.rect.size.x *= scale;
        batch.clip.rect.size.y *= scale;
    }
    return data;
}

void UiRuntime::set_focus_scope(std::string root_id, std::span<View*> active_roots) {
    input_router_.set_focus_scope(std::move(root_id), active_roots);
}

void UiRuntime::cancel_interaction_for_root(View& root) {
    input_router_.cancel_interaction_for_root(root);
}

void UiRuntime::clear_interaction_state(std::span<View*> active_roots) {
    input_router_.clear_interaction_state(active_roots);
}

}  // namespace snt::ui
