// Retained-MUI base view contract.
//
// This module owns only common retained-tree state, layout lifecycle, and
// generic ViewModel subscriptions. Concrete widgets, containers, dragging,
// and animation live in their own headers so consumers do not inherit every
// UI feature merely by accepting a View reference.

#pragma once

#include "retained_mui_arc.h"
#include "retained_mui_view_model.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace snt::ui {

class View {
public:
    using InputHandler = std::function<UiEventReply(const UiInputEvent&)>;

    explicit View(std::string id = {});
    virtual ~View() = default;

    View(const View&) = delete;
    View& operator=(const View&) = delete;

    virtual ViewKind kind() const { return ViewKind::View; }

    void set_id(std::string id) { id_ = std::move(id); }
    const std::string& id() const { return id_; }

    void set_layout_params(LayoutParams params) {
        layout_params_ = std::move(params);
        mark_layout_dirty();
    }
    const LayoutParams& layout_params() const { return layout_params_; }

    void set_background(Color color, float radius = 0.0f);
    void set_visibility(Visibility visibility) {
        if (visibility_ == visibility) return;
        visibility_ = visibility;
        mark_layout_dirty();
    }
    Visibility visibility() const { return visibility_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    void set_hit_test_visible(bool visible) { hit_test_visible_ = visible; }
    bool hit_test_visible() const { return hit_test_visible_; }
    void set_focusable(bool focusable) { focusable_ = focusable; }
    bool focusable() const { return focusable_; }
    UiInteractionState interaction_state() const { return interaction_state_; }
    void set_input_handler(InputHandler handler) { input_handler_ = std::move(handler); }

    Vec2 measured_size() const { return measured_size_; }
    Rect bounds() const { return bounds_; }
    bool layout_dirty() const { return layout_dirty_; }

    // RAII value binding for controls that do not expose text. The
    // subscription is owned by this retained View and is disconnected before
    // the concrete widget is destroyed.
    void bind_value(ViewModel& model, std::string key, ViewModel::Observer observer);

    virtual void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine);
    virtual void layout(Rect bounds);
    virtual void paint(Arc2DCommandBuffer& out,
                       TextEngine& text_engine,
                       const UiTheme& theme) const;

    virtual UiEventReply on_input_event(const UiInputEvent& event);
    bool hit_test(Vec2 point) const;

    // Parents can restrict child hit testing without changing child bounds.
    // ScrollView uses this to prevent clipped-off controls from receiving
    // clicks or wheel events outside its viewport.
    virtual bool accepts_child_input(Vec2 point) const {
        (void)point;
        return true;
    }

protected:
    friend class UiRuntime;
    friend class UiInputRouter;
    friend class ViewGroup;

    static float resolve_axis(float requested, MeasureSpec spec, float desired);
    static float clamp_axis(float value, MeasureSpec spec);
    void mark_layout_dirty();
    void attach_parent(View* parent) { parent_ = parent; }
    virtual void set_interaction_state(UiInteractionState state) {
        interaction_state_ = state;
    }

    std::string id_;
    LayoutParams layout_params_{};
    Visibility visibility_ = Visibility::Visible;
    bool enabled_ = true;
    bool hit_test_visible_ = false;
    bool focusable_ = false;
    UiInteractionState interaction_state_ = UiInteractionState::None;
    Vec2 measured_size_{};
    Rect bounds_{};
    std::optional<DrawRectCommand> background_;
    InputHandler input_handler_;
    std::vector<ViewModel::Subscription> bindings_;
    View* parent_ = nullptr;
    bool layout_dirty_ = true;
    bool has_layout_viewport_ = false;
    Vec2 last_layout_viewport_{};
};

}  // namespace snt::ui
