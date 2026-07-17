// Retained-MUI pointer, keyboard, focus, drag, and interaction-state routing.

#pragma once

#include "retained_mui_drag.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace snt::ui {

class UiTextInputService;

// A borrowed snapshot for native text-input synchronization. The string views
// remain valid until the router's focus state changes.
struct UiFocusedTextInput {
    std::string_view root_id;
    std::string_view view_id;
    Rect bounds;
};

// Borrowed only during the current host frame. UiRuntime uses it to resolve
// declarative hover services such as automatic Tooltip without leaking a
// retained View pointer outside the UI subsystem.
struct UiHoveredView {
    std::string_view root_id;
    const View* view = nullptr;
};

class UiInputRouter {
public:
    UiInputRouter();

    void begin_frame(UiInputState input, std::span<View*> active_roots = {});
    bool dispatch_pointer_input(View& root);
    bool dispatch_keyboard_input(View& root, UiTextInputService& text_input_service);
    void end_frame(std::span<View*> active_roots = {});
    void synchronize_interaction_state(View& root);

    // Returns a value-only session so callers never retain a dangling View.
    const UiDragSession* drag_session() const {
        return drag_session_ ? &*drag_session_ : nullptr;
    }

    std::optional<Rect> focused_text_input_bounds(const View& root) const;
    std::optional<UiFocusedTextInput> focused_text_input(
        std::span<View*> active_roots) const;
    std::optional<UiHoveredView> hovered_view(std::span<View*> active_roots) const;

    void set_focus_scope(std::string root_id, std::span<View*> active_roots = {});
    void cancel_interaction_for_root(View& root);
    void clear_interaction_state(std::span<View*> active_roots = {});

private:
    struct ElementId {
        std::string root;
        std::string view;

        bool matches(std::string_view root_id, std::string_view view_id) const {
            return root == root_id && view == view_id;
        }
        bool empty() const { return root.empty() || view.empty(); }
        void clear() { root.clear(); view.clear(); }
    };

    void update_drag_hover(View& root, const std::vector<View*>& hit_path);
    bool cancel_drag_for_root(View& root);
    void cancel_active_drag(std::span<View*> active_roots);
    void clear_focus_for_root(View& root);
    bool focus_view(View& root, std::string_view view_id);
    bool move_focus(View& root, bool reverse);
    // Resolves the current focus through an interactive retained path. A
    // hidden, disabled, removed, or no-longer-focusable target is released
    // through clear_focus_for_root() before any further input is routed.
    bool resolve_interactive_focus(View& root);
    static void collect_focusable_view_ids(const View& view,
                                           std::vector<std::string>& out);
    static View* find_active_root(std::span<View*> active_roots,
                                  std::string_view root_id);

    UiInputState input_{};
    std::array<bool, 3> previous_pointer_held_{};
    std::array<bool, 3> pointer_released_{};
    ElementId hovered_;
    ElementId focused_;
    ElementId pointer_capture_;
    std::optional<UiDragSession> drag_session_;
    std::string focus_scope_root_;
    // Borrowed only between begin_frame/end_frame so focus changes can
    // deliver lifecycle events while the previous root is still alive.
    std::vector<View*> active_roots_;
    std::vector<View*> hit_path_scratch_;
    std::vector<View*> event_path_scratch_;
};

}  // namespace snt::ui
