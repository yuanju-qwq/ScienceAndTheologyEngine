#define SNT_LOG_CHANNEL "ui.input"
#include "retained_mui_input_router.h"

#include "retained_mui_layout.h"
#include "retained_mui_text_input.h"
#include "retained_mui_text_input_service.h"

#include "core/log.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace snt::ui {

namespace {

bool build_hit_path(View& view, Vec2 point, std::vector<View*>& out) {
    // A disabled container disables its retained subtree for input as well as
    // for focus traversal. This prevents an enabled child from accidentally
    // receiving a pointer event through a disabled form or modal section.
    if (view.visibility() != Visibility::Visible || !view.enabled()) return false;

    out.push_back(&view);
    if (auto* group = dynamic_cast<ViewGroup*>(&view);
        group && view.accepts_child_input(point)) {
        auto& children = group->children();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (build_hit_path(**it, point, out)) return true;
        }
    }
    if (view.hit_test(point)) return true;

    out.pop_back();
    return false;
}

// Input dispatch requires a currently interactive path. Lifecycle cleanup
// instead needs to find retained nodes after a parent has become hidden or
// disabled, while the objects are still alive.
enum class PathLookupMode : uint8_t {
    Interactive,
    Retained,
};

template <typename ViewType>
bool build_path_to_id(ViewType& view,
                      std::string_view id,
                      std::vector<ViewType*>& out,
                      PathLookupMode mode = PathLookupMode::Interactive) {
    if (mode == PathLookupMode::Interactive &&
        (view.visibility() != Visibility::Visible || !view.enabled())) {
        return false;
    }

    out.push_back(&view);
    if (view.id() == id) return true;
    using GroupType = std::conditional_t<std::is_const_v<ViewType>,
                                         const ViewGroup,
                                         ViewGroup>;
    using ChildViewType = std::conditional_t<std::is_const_v<ViewType>,
                                             const View,
                                             View>;
    if (auto* group = dynamic_cast<GroupType*>(&view)) {
        for (auto& child : group->children()) {
            if (build_path_to_id(static_cast<ChildViewType&>(*child), id, out, mode)) {
                return true;
            }
        }
    }
    out.pop_back();
    return false;
}

template <typename ViewType>
bool is_focus_candidate(const std::vector<ViewType*>& path) {
    if (path.empty()) return false;
    if (!std::all_of(path.begin(), path.end(), [](const ViewType* view) {
            return view && view->visibility() == Visibility::Visible && view->enabled();
        })) {
        return false;
    }
    const ViewType* const target = path.back();
    return target->focusable() && !target->id().empty();
}

UiEventReply dispatch_event_path(const std::vector<View*>& path, UiInputEvent event) {
    UiEventReply result = UiEventReply::Ignored;
    if (path.empty()) return result;

    for (size_t index = 0; index + 1 < path.size(); ++index) {
        event.phase = UiEventPhase::Capture;
        const UiEventReply reply = path[index]->on_input_event(event);
        if (reply == UiEventReply::StopPropagation) return reply;
        if (reply == UiEventReply::Handled) result = reply;
    }

    event.phase = UiEventPhase::Target;
    const UiEventReply target_reply = path.back()->on_input_event(event);
    if (target_reply == UiEventReply::StopPropagation) return target_reply;
    if (target_reply == UiEventReply::Handled) result = target_reply;

    for (size_t index = path.size() - 1; index > 0; --index) {
        event.phase = UiEventPhase::Bubble;
        const UiEventReply reply = path[index - 1]->on_input_event(event);
        if (reply == UiEventReply::StopPropagation) return reply;
        if (reply == UiEventReply::Handled) result = reply;
    }
    return result;
}

View* deepest_focusable_view(const std::vector<View*>& path) {
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if ((*it)->focusable() && (*it)->enabled()) return *it;
    }
    return nullptr;
}

UiPointerButton pointer_button_for_index(size_t index) {
    switch (index) {
        case 0: return UiPointerButton::Primary;
        case 1: return UiPointerButton::Middle;
        case 2: return UiPointerButton::Secondary;
        default: return UiPointerButton::None;
    }
}

}  // namespace

UiInputRouter::UiInputRouter() {
    hit_path_scratch_.reserve(32);
    event_path_scratch_.reserve(32);
}

void UiInputRouter::begin_frame(UiInputState input, std::span<View*> active_roots) {
    input_ = std::move(input);
    active_roots_.assign(active_roots.begin(), active_roots.end());
    for (size_t index = 0; index < previous_pointer_held_.size(); ++index) {
        pointer_released_[index] = input_.pointer_released[index] ||
            (previous_pointer_held_[index] && !input_.pointer_held[index]);
        previous_pointer_held_[index] = input_.pointer_held[index];
    }

    hovered_.clear();
    if (!input_.pointer_enabled) {
        cancel_active_drag(active_roots);
        pointer_capture_.clear();
    }
}

bool UiInputRouter::dispatch_pointer_input(View& root) {
    if (!input_.pointer_enabled) return false;
    const std::string& root_id = root.id();

    // A captured button owns pointer-up even when another modal is now under
    // the cursor. The host therefore tries roots from top to bottom until it
    // reaches this stable root id.
    if (!pointer_capture_.empty() && pointer_capture_.root != root_id) return false;

    std::vector<View*>& hit_path = hit_path_scratch_;
    hit_path.clear();
    const bool pointer_over_target = build_hit_path(root, input_.pointer_position, hit_path);
    const bool has_hit_target = pointer_capture_.empty() && pointer_over_target;
    if (has_hit_target && !root_id.empty() && !hit_path.back()->id().empty()) {
        hovered_ = {root_id, hit_path.back()->id()};
    }

    bool claimed = false;
    if (has_hit_target &&
        (input_.scroll_delta.x != 0.0f || input_.scroll_delta.y != 0.0f)) {
        const UiEventReply reply = dispatch_event_path(hit_path, {
            .type = UiInputEventType::PointerScroll,
            .pointer_position = input_.pointer_position,
            .scroll_delta = input_.scroll_delta,
        });
        claimed = reply == UiEventReply::Handled || reply == UiEventReply::StopPropagation;
    }

    for (size_t index = 0; index < input_.pointer_pressed.size(); ++index) {
        if (!input_.pointer_pressed[index] || !has_hit_target) continue;

        View* const target = hit_path.back();
        if (!root_id.empty() && !target->id().empty()) {
            pointer_capture_ = {root_id, target->id()};
        }

        if (View* const focus_target = deepest_focusable_view(hit_path);
            focus_target && !root_id.empty() && !focus_target->id().empty()) {
            (void)focus_view(root, focus_target->id());
        } else if (!focused_.empty()) {
            if (focused_.root == root_id) {
                clear_focus_for_root(root);
            } else if (View* const previous_root = find_active_root(active_roots_, focused_.root)) {
                clear_focus_for_root(*previous_root);
            } else {
                focused_.clear();
            }
        }

        dispatch_event_path(hit_path, {
            .type = UiInputEventType::PointerDown,
            .pointer_position = input_.pointer_position,
            .pointer_button = pointer_button_for_index(index),
            .modifiers = input_.modifiers,
        });
        if (!drag_session_ && (index == 0 || index == 2) && target->enabled() &&
            !root_id.empty() && !target->id().empty()) {
            if (auto* source = dynamic_cast<UiDragSource*>(target)) {
                const UiPointerButton pointer_button = pointer_button_for_index(index);
                if (auto payload = source->begin_drag({
                        .pointer_button = pointer_button,
                        .pointer_position = input_.pointer_position,
                    }); payload && !payload->type.empty()) {
                    UiDragSession session;
                    session.source_root_id_ = root_id;
                    session.source_view_id_ = target->id();
                    session.payload_ = std::move(*payload);
                    session.pointer_button_ = pointer_button;
                    drag_session_ = std::move(session);
                    source->on_drag_event({
                        .type = UiDragEventType::Begin,
                        .source_id = target->id(),
                        .payload = drag_session_->payload_,
                    });
                }
            }
        }
        claimed = true;
    }

    for (size_t index = 0; index < pointer_released_.size(); ++index) {
        if (!pointer_released_[index]) continue;
        const UiPointerButton pointer_button = pointer_button_for_index(index);
        const bool releases_active_drag = drag_session_ &&
            drag_session_->pointer_button_ == pointer_button;

        if (!pointer_capture_.empty() && pointer_capture_.root == root_id) {
            event_path_scratch_.clear();
            if (build_path_to_id(root, pointer_capture_.view, event_path_scratch_)) {
                const bool activation = event_path_scratch_.back()->hit_test(input_.pointer_position);
                dispatch_event_path(event_path_scratch_, {
                    .type = UiInputEventType::PointerUp,
                    .pointer_position = input_.pointer_position,
                    .pointer_button = pointer_button,
                    .activation = activation,
                });
            }
            if (releases_active_drag && drag_session_ &&
                drag_session_->source_root_id_ == root_id) {
                const UiDragSession session = *drag_session_;
                UiDragSource* source = nullptr;
                event_path_scratch_.clear();
                if (build_path_to_id(root, session.source_view_id_, event_path_scratch_)) {
                    source = dynamic_cast<UiDragSource*>(event_path_scratch_.back());
                }

                View* target_view = nullptr;
                UiDropTarget* drop_target = nullptr;
                if (pointer_over_target) {
                    for (auto it = hit_path.rbegin(); it != hit_path.rend(); ++it) {
                        View* const candidate = *it;
                        if (!candidate->enabled() || candidate->id().empty() ||
                            candidate->id() == session.source_view_id_) {
                            continue;
                        }
                        auto* const candidate_target = dynamic_cast<UiDropTarget*>(candidate);
                        if (candidate_target && candidate_target->accepts_drop(session.payload_)) {
                            target_view = candidate;
                            drop_target = candidate_target;
                            break;
                        }
                    }
                }
                const bool has_drop_target = target_view && drop_target;
                if (has_drop_target) {
                    const UiDragEvent event{
                        .type = UiDragEventType::Drop,
                        .source_id = session.source_view_id_,
                        .target_id = target_view->id(),
                        .payload = session.payload_,
                    };
                    // Clear first: a command handler may synchronously change
                    // screen visibility, which must not send a second cancel.
                    drag_session_.reset();
                    if (source) source->on_drag_event(event);
                    drop_target->on_drag_event(event);
                } else {
                    // A previous hover target must also receive cancellation
                    // so visual state and command-side drag transactions do
                    // not remain active after an outside/invalid drop.
                    cancel_drag_for_root(root);
                }
            }
            if (!drag_session_ || releases_active_drag) pointer_capture_.clear();
            claimed = true;
        } else if (has_hit_target) {
            dispatch_event_path(hit_path, {
                .type = UiInputEventType::PointerUp,
                .pointer_position = input_.pointer_position,
                .pointer_button = pointer_button,
            });
            claimed = true;
        }
    }

    if (!pointer_capture_.empty() && pointer_capture_.root == root_id) {
        event_path_scratch_.clear();
        if (build_path_to_id(root, pointer_capture_.view, event_path_scratch_)) {
            dispatch_event_path(event_path_scratch_, {
                .type = UiInputEventType::PointerMove,
                .pointer_position = input_.pointer_position,
            });
        } else {
            pointer_capture_.clear();
        }
        update_drag_hover(root, pointer_over_target ? hit_path : std::vector<View*>{});
        return true;
    }

    if (has_hit_target) {
        dispatch_event_path(hit_path, {
            .type = UiInputEventType::PointerMove,
            .pointer_position = input_.pointer_position,
        });
        update_drag_hover(root, hit_path);
        return true;
    }
    return claimed;
}

bool UiInputRouter::dispatch_keyboard_input(View& root, UiTextInputService& text_input_service) {
    if (!focus_scope_root_.empty() && root.id() != focus_scope_root_) return false;
    if (focused_.root == root.id()) {
        (void)resolve_interactive_focus(root);
    }
    if (input_.pressed_keys.empty() && input_.text_commits.empty() &&
        input_.text_compositions.empty()) {
        return false;
    }

    bool claimed = false;

    for (UiKey key : input_.pressed_keys) {
        if (key == UiKey::Tab) {
            const bool reverse = has_ui_key_modifier(input_.modifiers, UiKeyModifier::Shift);
            claimed = move_focus(root, reverse) || claimed;
            continue;
        }

        if (focused_.empty() || focused_.root != root.id()) continue;
        if (!resolve_interactive_focus(root)) continue;
        dispatch_event_path(event_path_scratch_, {
            .type = UiInputEventType::KeyDown,
            .key = key,
            .modifiers = input_.modifiers,
        });
        claimed = true;
        if (!event_path_scratch_.empty()) {
            if (auto* text_input = dynamic_cast<TextInput*>(event_path_scratch_.back())) {
                static_cast<void>(text_input_service.handle_clipboard_shortcut(
                    *text_input, key, input_.modifiers, focused_.root, focused_.view));
            }
        }
    }
    for (const std::string& text : input_.text_commits) {
        if (focused_.empty() || focused_.root != root.id()) continue;
        if (!resolve_interactive_focus(root)) continue;
        dispatch_event_path(event_path_scratch_, {
            .type = UiInputEventType::TextCommit,
            .text = text,
        });
        claimed = true;
    }
    for (const UiTextComposition& composition : input_.text_compositions) {
        if (focused_.empty() || focused_.root != root.id()) continue;
        if (!resolve_interactive_focus(root)) continue;
        dispatch_event_path(event_path_scratch_, {
            .type = UiInputEventType::TextComposition,
            .text = composition.text,
            .composition_start = composition.start,
            .composition_length = composition.length,
        });
        claimed = true;
    }
    return claimed;
}

void UiInputRouter::collect_focusable_view_ids(const View& view,
                                           std::vector<std::string>& out) {
    // A disabled or hidden container makes its complete retained subtree
    // ineligible, even when a child has not been individually disabled.
    if (view.visibility() != Visibility::Visible || !view.enabled()) return;
    if (view.focusable() && !view.id().empty()) out.push_back(view.id());
    if (const auto* group = dynamic_cast<const ViewGroup*>(&view)) {
        for (const auto& child : group->children()) {
            collect_focusable_view_ids(*child, out);
        }
    }
}

bool UiInputRouter::focus_view(View& root, std::string_view view_id) {
    if (root.id().empty() || view_id.empty()) return false;
    const std::string stable_view_id(view_id);
    if (focused_.matches(root.id(), stable_view_id)) {
        return resolve_interactive_focus(root);
    }

    event_path_scratch_.clear();
    if (!build_path_to_id(root, stable_view_id, event_path_scratch_) ||
        !is_focus_candidate(event_path_scratch_)) {
        return false;
    }

    if (!focused_.empty()) {
        if (focused_.root == root.id()) {
            clear_focus_for_root(root);
        } else if (View* const previous_root = find_active_root(active_roots_, focused_.root)) {
            clear_focus_for_root(*previous_root);
        } else {
            // An embedding host may route a root directly without supplying
            // an active-root list. Do not retain an unverifiable old ID.
            focused_.clear();
        }
    }

    // FocusLost handlers may synchronously rebuild or hide a retained tree.
    // Re-resolve the target by stable ID before publishing FocusGained.
    event_path_scratch_.clear();
    if (!build_path_to_id(root, stable_view_id, event_path_scratch_) ||
        !is_focus_candidate(event_path_scratch_)) {
        return false;
    }
    focused_ = {root.id(), stable_view_id};
    dispatch_event_path(event_path_scratch_, {.type = UiInputEventType::FocusGained});
    return true;
}

bool UiInputRouter::move_focus(View& root, bool reverse) {
    std::vector<std::string> candidates;
    candidates.reserve(8);
    collect_focusable_view_ids(root, candidates);
    if (candidates.empty()) {
        if (focused_.root == root.id()) clear_focus_for_root(root);
        return false;
    }

    const auto current = focused_.root == root.id()
        ? std::find(candidates.begin(), candidates.end(), focused_.view)
        : candidates.end();
    size_t next_index = 0;
    if (current == candidates.end()) {
        next_index = reverse ? candidates.size() - 1 : 0;
    } else if (reverse) {
        next_index = current == candidates.begin()
            ? candidates.size() - 1
            : static_cast<size_t>(std::distance(candidates.begin(), current) - 1);
    } else {
        next_index = static_cast<size_t>(std::distance(candidates.begin(), current) + 1) %
            candidates.size();
    }
    return focus_view(root, candidates[next_index]);
}

bool UiInputRouter::resolve_interactive_focus(View& root) {
    if (focused_.empty() || focused_.root != root.id()) return false;

    event_path_scratch_.clear();
    if (build_path_to_id(root, focused_.view, event_path_scratch_) &&
        is_focus_candidate(event_path_scratch_)) {
        return true;
    }

    clear_focus_for_root(root);
    return false;
}

void UiInputRouter::update_drag_hover(View& root, const std::vector<View*>& hit_path) {
    if (!drag_session_ || drag_session_->source_root_id_ != root.id()) return;

    View* target_view = nullptr;
    UiDropTarget* drop_target = nullptr;
    for (auto it = hit_path.rbegin(); it != hit_path.rend(); ++it) {
        View* const candidate = *it;
        if (!candidate->enabled() || candidate->id().empty() ||
            candidate->id() == drag_session_->source_view_id_) {
            continue;
        }
        auto* const candidate_target = dynamic_cast<UiDropTarget*>(candidate);
        if (candidate_target && candidate_target->accepts_drop(drag_session_->payload_)) {
            target_view = candidate;
            drop_target = candidate_target;
            break;
        }
    }
    const std::string next_view_id = target_view ? target_view->id() : std::string{};
    if (drag_session_->hovered_root_id_ == root.id() &&
        drag_session_->hovered_view_id_ == next_view_id) {
        return;
    }

    if (!drag_session_->hovered_view_id_.empty()) {
        event_path_scratch_.clear();
        if (build_path_to_id(root, drag_session_->hovered_view_id_, event_path_scratch_)) {
            if (auto* previous = dynamic_cast<UiDropTarget*>(event_path_scratch_.back())) {
                previous->on_drag_event({
                    .type = UiDragEventType::Leave,
                    .source_id = drag_session_->source_view_id_,
                    .target_id = drag_session_->hovered_view_id_,
                    .payload = drag_session_->payload_,
                });
            }
        }
    }
    drag_session_->hovered_root_id_ = target_view ? root.id() : std::string{};
    drag_session_->hovered_view_id_ = next_view_id;
    if (drop_target) {
        drop_target->on_drag_event({
            .type = UiDragEventType::Enter,
            .source_id = drag_session_->source_view_id_,
            .target_id = next_view_id,
            .payload = drag_session_->payload_,
        });
    }
}

bool UiInputRouter::cancel_drag_for_root(View& root) {
    if (!drag_session_ || drag_session_->source_root_id_ != root.id()) return false;

    const UiDragSession session = std::move(*drag_session_);
    drag_session_.reset();

    UiDragSource* source = nullptr;
    event_path_scratch_.clear();
    if (build_path_to_id(root, session.source_view_id_, event_path_scratch_)) {
        source = dynamic_cast<UiDragSource*>(event_path_scratch_.back());
    }

    UiDropTarget* target = nullptr;
    if (!session.hovered_view_id_.empty()) {
        event_path_scratch_.clear();
        if (build_path_to_id(root, session.hovered_view_id_, event_path_scratch_)) {
            target = dynamic_cast<UiDropTarget*>(event_path_scratch_.back());
        }
    }

    const UiDragEvent event{
        .type = UiDragEventType::Cancel,
        .source_id = session.source_view_id_,
        .target_id = session.hovered_view_id_,
        .payload = session.payload_,
    };
    if (source) {
        source->on_drag_event(event);
    } else {
        SNT_LOG_WARN("MUI drag cancellation lost source: root='%s' view='%s'",
                     session.source_root_id_.c_str(), session.source_view_id_.c_str());
    }
    if (target) target->on_drag_event(event);
    return true;
}

View* UiInputRouter::find_active_root(std::span<View*> active_roots,
                                  std::string_view root_id) {
    const auto found = std::find_if(active_roots.begin(), active_roots.end(),
        [root_id](View* root) { return root && root->id() == root_id; });
    return found == active_roots.end() ? nullptr : *found;
}

void UiInputRouter::cancel_active_drag(std::span<View*> active_roots) {
    if (!drag_session_) return;
    if (View* const root = find_active_root(active_roots, drag_session_->source_root_id_)) {
        (void)cancel_drag_for_root(*root);
        return;
    }

    // This is a host contract violation rather than a per-frame diagnostic:
    // it occurs at most once for the discarded session and retains no view
    // pointer after the source root has disappeared.
    SNT_LOG_WARN("MUI discarded drag without an active source root: root='%s' view='%s'",
                 drag_session_->source_root_id_.c_str(),
                 drag_session_->source_view_id_.c_str());
    drag_session_.reset();
}

void UiInputRouter::clear_focus_for_root(View& root) {
    if (focused_.root != root.id()) return;

    event_path_scratch_.clear();
    if (build_path_to_id(root, focused_.view, event_path_scratch_,
                         PathLookupMode::Retained)) {
        dispatch_event_path(event_path_scratch_, {.type = UiInputEventType::FocusLost});
    }
    focused_.clear();
}

void UiInputRouter::end_frame(std::span<View*> active_roots) {
    // A modal can become visible between pointer-down and pointer-up. The
    // release must not activate a lower screen, but the retained source and
    // hovered target still receive a deterministic DragCancel.
    const bool any_release = std::any_of(pointer_released_.begin(), pointer_released_.end(),
                                         [](bool released) { return released; });
    bool active_drag_released = false;
    if (drag_session_) {
        for (size_t index = 0; index < pointer_released_.size(); ++index) {
            if (pointer_released_[index] &&
                drag_session_->pointer_button_ == pointer_button_for_index(index)) {
                active_drag_released = true;
                break;
            }
        }
    }
    const bool drag_outside_focus_scope = drag_session_ && !focus_scope_root_.empty() &&
        drag_session_->source_root_id_ != focus_scope_root_;
    if (!input_.pointer_enabled || active_drag_released || drag_outside_focus_scope) {
        cancel_active_drag(active_roots);
    }
    if (!input_.pointer_enabled || (!drag_session_ && any_release) || active_drag_released) {
        pointer_capture_.clear();
    }
    active_roots_.clear();
}

void UiInputRouter::synchronize_interaction_state(View& root) {
    const std::string& root_id = root.id();
    if (focused_.root == root_id) {
        (void)resolve_interactive_focus(root);
    }
    const auto synchronize = [this, &root_id](auto&& self, View& view) -> void {
        UiInteractionState state = UiInteractionState::None;
        if (!view.enabled()) {
            state = UiInteractionState::Disabled;
        } else if (!root_id.empty() && !view.id().empty()) {
            if (hovered_.matches(root_id, view.id())) {
                state = state | UiInteractionState::Hovered;
            }
            if (pointer_capture_.matches(root_id, view.id())) {
                state = state | UiInteractionState::Pressed;
            }
            if (focused_.matches(root_id, view.id())) {
                state = state | UiInteractionState::Focused;
            }
        }
        view.set_interaction_state(state);

        if (auto* group = dynamic_cast<ViewGroup*>(&view)) {
            for (auto& child : group->children()) self(self, *child);
        }
    };
    synchronize(synchronize, root);
}

std::optional<Rect> UiInputRouter::focused_text_input_bounds(const View& root) const {
    if (focused_.empty() || focused_.root != root.id()) return std::nullopt;
    std::vector<const View*> path;
    path.reserve(8);
    if (!build_path_to_id(root, focused_.view, path) || !is_focus_candidate(path)) {
        return std::nullopt;
    }
    const auto* text_input = dynamic_cast<const TextInput*>(path.back());
    return text_input ? std::optional<Rect>{text_input->ime_bounds()} : std::nullopt;
}

std::optional<UiFocusedTextInput> UiInputRouter::focused_text_input(
    std::span<View*> active_roots) const {
    if (focused_.empty()) return std::nullopt;
    View* const root = find_active_root(active_roots, focused_.root);
    if (!root) return std::nullopt;
    const auto bounds = focused_text_input_bounds(*root);
    if (!bounds) return std::nullopt;
    return UiFocusedTextInput{
        .root_id = focused_.root,
        .view_id = focused_.view,
        .bounds = *bounds,
    };
}

void UiInputRouter::set_focus_scope(std::string root_id, std::span<View*> active_roots) {
    if (focus_scope_root_ == root_id) return;
    focus_scope_root_ = std::move(root_id);
    if (!focus_scope_root_.empty() && focused_.root != focus_scope_root_) {
        if (View* const focused_root = find_active_root(active_roots, focused_.root)) {
            clear_focus_for_root(*focused_root);
        } else {
            focused_.clear();
        }
    }
    if (!focus_scope_root_.empty() && pointer_capture_.root != focus_scope_root_) {
        pointer_capture_.clear();
    }
    if (!focus_scope_root_.empty() && drag_session_ &&
        drag_session_->source_root_id_ != focus_scope_root_) {
        cancel_active_drag(active_roots);
    }
}

void UiInputRouter::cancel_interaction_for_root(View& root) {
    const std::string& root_id = root.id();
    if (hovered_.root == root_id) hovered_.clear();
    clear_focus_for_root(root);
    if (pointer_capture_.root == root_id) pointer_capture_.clear();
    (void)cancel_drag_for_root(root);
    if (focus_scope_root_ == root_id) focus_scope_root_.clear();
}

void UiInputRouter::clear_interaction_state(std::span<View*> active_roots) {
    hovered_.clear();
    if (View* const focused_root = find_active_root(active_roots, focused_.root)) {
        clear_focus_for_root(*focused_root);
    } else {
        focused_.clear();
    }
    pointer_capture_.clear();
    cancel_active_drag(active_roots);
    focus_scope_root_.clear();
    previous_pointer_held_.fill(false);
    pointer_released_.fill(false);
    active_roots_.clear();
}

}  // namespace snt::ui
