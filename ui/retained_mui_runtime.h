// Retained-MUI per-frame orchestration, layout, and painting facade.

#pragma once

#include "retained_mui_input_router.h"
#include "retained_mui_screen_stack.h"
#include "retained_mui_text_input_service.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace snt::ui {

struct UiFrameResult {
    Arc2DCommandBuffer commands;
    UiDrawData draw_data;
};

class UiRuntime {
public:
    UiRuntime(const snt::core::RuntimePathResolver& paths,
              TextEngineConfig config = {},
              UiTheme theme = {});

    TextEngine& text_engine() { return text_engine_; }
    const TextEngine& text_engine() const { return text_engine_; }
    bool text_available() const { return text_engine_.available(); }
    const std::string& text_initialization_error() const {
        return text_engine_.initialization_error();
    }
    UiImageRegistry& images() { return images_; }
    const UiImageRegistry& images() const { return images_; }
    UiLayerStack& layers() { return layers_; }
    const UiLayerStack& layers() const { return layers_; }

    void set_theme(UiTheme theme) { theme_ = std::move(theme); }
    const UiTheme& theme() const { return theme_; }

    // The host updates this whenever framebuffer size, display DPI, or the
    // user preference changes. Existing retained roots relayout lazily on the
    // next layout call when the logical viewport differs.
    void set_viewport(UiViewport viewport);
    const UiViewport& viewport() const { return viewport_; }
    void set_user_scale(float scale);

    // Host services are shared ownership so a retained runtime never keeps a
    // dangling native-platform object. They are optional for headless tests.
    void set_clipboard(std::shared_ptr<IUiClipboard> clipboard);
    void set_text_input_platform(std::shared_ptr<IUiTextInputPlatform> platform);

    // Hosts first layout all submitted roots, route one input frame from the
    // highest interactive layer downward, then synchronize states and paint.
    // Root and child ids must be unique within their UI layer so transient
    // view rebuilding can safely retain focus, hover, and pointer capture.
    void layout(View& root, Vec2 viewport);
    // Active roots let the runtime finish focus and drag lifecycles even when
    // a modal or native input capture prevents normal pointer routing.
    void begin_input_frame(UiInputState input, std::span<View*> active_roots = {});
    // Each dispatcher returns true when this root owns that input channel and
    // lower layers must not receive that channel for the current host frame.
    bool dispatch_pointer_input(View& root);
    bool dispatch_keyboard_input(View& root);
    // Clears a capture whose pointer-up could not be routed because a newly
    // visible blocking layer superseded its former root. The host invokes this
    // once after all layer policies have been evaluated for the frame.
    void end_input_frame(std::span<View*> active_roots = {});
    void synchronize_interaction_state(View& root);
    // Returns a borrowed value-only session while a drag is active. It never
    // exposes a raw retained view pointer and becomes null at drop/cancel.
    const UiDragSession* drag_session() const {
        return input_router_.drag_session();
    }

    // The client host uses this to place the native IME candidate window. It
    // returns no value unless this root owns the currently focused TextInput.
    std::optional<Rect> focused_text_input_bounds(const View& root) const;
    // Synchronizes native IME activation after input routing. `enabled` lets
    // hosts suppress native text input during pointer-lock/gameplay capture.
    void synchronize_text_input_platform(std::span<View*> active_roots,
                                         bool enabled = true);
    UiFrameResult paint(View& root);
    UiDrawData build_draw_data(const Arc2DCommandBuffer& commands);
    void set_focus_scope(std::string root_id, std::span<View*> active_roots = {});
    // UiLayerStack invokes this while `root` is still valid. It delivers
    // FocusLost and DragCancel before clearing retained interaction state.
    void cancel_interaction_for_root(View& root);
    void clear_interaction_state(std::span<View*> active_roots = {});

private:
    UnicodeTextEngine text_engine_;
    UiImageRegistry images_;
    UiLayerStack layers_;
    Arc2DRenderer renderer_;
    UiTheme theme_{};
    UiViewport viewport_{};
    UiInputRouter input_router_;
    UiTextInputService text_input_service_;
};

}  // namespace snt::ui
