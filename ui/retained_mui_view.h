// Retained-MUI view tree, widgets, and layout primitives.

#pragma once

#include "retained_mui_arc.h"
#include "retained_mui_view_model.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

    void set_bound_text_key(std::string key) { bound_text_key_ = std::move(key); }
    const std::string& bound_text_key() const { return bound_text_key_; }

    void bind_text(ViewModel& model, std::string key);
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
    std::string bound_text_key_;
    InputHandler input_handler_;
    std::vector<ViewModel::Subscription> bindings_;
    View* parent_ = nullptr;
    bool layout_dirty_ = true;
    bool has_layout_viewport_ = false;
    Vec2 last_layout_viewport_{};
};

class TextView : public View {
public:
    explicit TextView(std::string id = {});
    ViewKind kind() const override { return ViewKind::TextView; }

    void set_text(std::string text) {
        if (text_ == text) return;
        text_ = std::move(text);
        dirty_layout_ = true;
        mark_layout_dirty();
    }
    const std::string& text() const { return text_; }

    void set_text_style(TextStyle style) {
        style_ = style;
        dirty_layout_ = true;
        mark_layout_dirty();
    }
    const TextStyle& text_style() const { return style_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;

protected:
    std::string text_;
    TextStyle style_{};
    mutable TextLayout cached_layout_{};
    mutable bool dirty_layout_ = true;
};

class Button : public TextView {
public:
    using ActivateHandler = std::function<void()>;

    explicit Button(std::string id = {});
    ViewKind kind() const override { return ViewKind::Button; }

    void set_on_activate(ActivateHandler handler) { activate_handler_ = std::move(handler); }
    bool activate() const;

    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;

private:
    ActivateHandler activate_handler_;
};

// UTF-8 editing base for TextInput and TextEditor. Platform text commits and
// IME preedit state arrive through UiInputEvent; clipboard and native IME
// services remain injected into UiRuntime instead of retained widgets.
class TextInput : public TextView {
public:
    using ChangeHandler = std::function<void(std::string_view)>;
    using SubmitHandler = std::function<void(std::string_view)>;

    explicit TextInput(std::string id = {});
    ViewKind kind() const override { return ViewKind::TextInput; }

    void set_text(std::string text);
    void set_text_silently(std::string text);
    void set_placeholder(std::string placeholder) {
        placeholder_ = std::move(placeholder);
        mark_layout_dirty();
    }
    const std::string& placeholder() const { return placeholder_; }
    void set_max_bytes(size_t max_bytes) { max_bytes_ = max_bytes; }
    size_t max_bytes() const { return max_bytes_; }
    void set_password(bool password) {
        if (password_ == password) return;
        password_ = password;
        mark_layout_dirty();
    }
    bool password() const { return password_; }
    void set_undo_limit(size_t limit);
    size_t undo_limit() const { return undo_limit_; }
    void set_on_change(ChangeHandler handler) { change_handler_ = std::move(handler); }
    void set_on_submit(SubmitHandler handler) { submit_handler_ = std::move(handler); }
    size_t cursor_byte_offset() const { return cursor_; }
    size_t selection_anchor_byte_offset() const { return selection_anchor_; }
    size_t selection_begin_byte_offset() const;
    size_t selection_end_byte_offset() const;
    bool has_selection() const { return cursor_ != selection_anchor_; }
    std::string selected_text() const;
    void select_all();
    bool undo();
    bool redo();
    [[nodiscard]] snt::core::Expected<void> copy_selection(IUiClipboard& clipboard) const;
    [[nodiscard]] snt::core::Expected<void> cut_selection(IUiClipboard& clipboard);
    [[nodiscard]] snt::core::Expected<void> paste_from(IUiClipboard& clipboard);
    Rect ime_bounds() const { return bounds(); }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;

protected:
    void set_multiline(bool multiline);
    bool multiline() const { return multiline_; }

private:
    struct EditState {
        std::string text;
        size_t cursor = 0;
        size_t selection_anchor = 0;
    };

    void replace_text(std::string text, bool notify, bool reset_history);
    bool apply_user_edit(std::string text, size_t cursor);
    void restore_edit_state(EditState state, bool notify);
    void record_undo_state();
    void clear_history();
    std::string normalize_text(std::string_view text) const;
    void erase_selection();
    void insert_text(std::string_view text);
    void erase_previous_codepoint();
    void erase_next_codepoint();
    void move_cursor_left(bool extend_selection);
    void move_cursor_right(bool extend_selection);
    void move_cursor_up(bool extend_selection);
    void move_cursor_down(bool extend_selection);
    void move_cursor_home(bool extend_selection);
    void move_cursor_end(bool extend_selection);
    void move_cursor_to(size_t offset, bool extend_selection);
    std::string display_text() const;

    std::string placeholder_;
    std::string composition_;
    int32_t composition_start_ = -1;
    int32_t composition_length_ = -1;
    size_t cursor_ = 0;
    size_t selection_anchor_ = 0;
    size_t max_bytes_ = 4096;
    size_t undo_limit_ = 128;
    std::vector<EditState> undo_history_;
    std::vector<EditState> redo_history_;
    bool multiline_ = false;
    bool password_ = false;
    ChangeHandler change_handler_;
    SubmitHandler submit_handler_;
};

// Retained multi-line editor for chat, notes, and future Mod configuration.
// It shares TextInput's UTF-8, IME, clipboard, selection, and history rules
// while Enter inserts a line break and Ctrl/Meta+Enter invokes submit.
class TextEditor final : public TextInput {
public:
    explicit TextEditor(std::string id = {});
    ViewKind kind() const override { return ViewKind::TextEditor; }

    void set_min_visible_lines(uint32_t lines);
    uint32_t min_visible_lines() const { return min_visible_lines_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;

private:
    uint32_t min_visible_lines_ = 3;
};

class Checkbox final : public TextView {
public:
    using ChangeHandler = std::function<void(bool)>;

    explicit Checkbox(std::string id = {});
    ViewKind kind() const override { return ViewKind::Checkbox; }

    void set_checked(bool checked, bool notify = false);
    bool checked() const { return checked_; }
    void set_on_change(ChangeHandler handler) { change_handler_ = std::move(handler); }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;

private:
    bool checked_ = false;
    ChangeHandler change_handler_;
};

class Slider final : public View {
public:
    using ChangeHandler = std::function<void(float)>;

    explicit Slider(std::string id = {});
    ViewKind kind() const override { return ViewKind::Slider; }

    void set_range(float minimum, float maximum);
    float minimum() const { return minimum_; }
    float maximum() const { return maximum_; }
    void set_step(float step) { step_ = std::max(0.0f, step); }
    float step() const { return step_; }
    void set_value(float value, bool notify = false);
    float value() const { return value_; }
    void set_on_change(ChangeHandler handler) { change_handler_ = std::move(handler); }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;

private:
    void set_value_from_pointer(float x);
    float normalized_value() const;

    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    float step_ = 0.0f;
    float value_ = 0.0f;
    ChangeHandler change_handler_;
};

class ImageView : public View {
public:
    explicit ImageView(std::string id = {});
    ViewKind kind() const override { return ViewKind::ImageView; }

    void set_image_key(std::string image_key) { image_key_ = std::move(image_key); }
    const std::string& image_key() const { return image_key_; }
    void set_tint(Color tint) { tint_ = tint; }
    Color tint() const { return tint_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;

private:
    std::string image_key_;
    Color tint_{255, 255, 255, 255};
};

// A stretchable panel/image whose source-space corners remain crisp at any
// destination size. Borders are logical UI units so they scale with the
// active DPI and user scale together with the rest of the layout.
class NineSliceView : public View {
public:
    explicit NineSliceView(std::string id = {});
    ViewKind kind() const override { return ViewKind::NineSliceView; }

    void set_image_key(std::string image_key) { image_key_ = std::move(image_key); }
    const std::string& image_key() const { return image_key_; }
    void set_borders(Insets borders) { borders_ = borders; mark_layout_dirty(); }
    Insets borders() const { return borders_; }
    void set_tint(Color tint) { tint_ = tint; }
    Color tint() const { return tint_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;

private:
    std::string image_key_;
    Insets borders_{};
    Color tint_{255, 255, 255, 255};
};

// Generic, value-only drag payload contract.
struct UiDragPayload {
    std::string type;
    std::string resource_key;
    int32_t count = 0;
};

enum class UiDragEventType : uint8_t {
    Begin,
    Enter,
    Leave,
    Drop,
    Cancel,
};

struct UiDragEvent {
    UiDragEventType type = UiDragEventType::Begin;
    std::string source_id;
    std::string target_id;
    UiDragPayload payload{};
};

// Drag sources receive the initiating pointer button as data rather than
// reading host input directly. SlotView uses Primary for a complete stack and
// Secondary for a half-stack split; custom sources may define their own
// value-only affordances without leaking platform events into the contract.
struct UiDragStartContext {
    UiPointerButton pointer_button = UiPointerButton::Primary;
    Vec2 pointer_position{};
};

// Views opt into dragging through narrow interfaces rather than runtime type
// checks. Both interfaces are safe extension points for game-owned widgets;
// the Mod facade remains value-only and never receives these native objects.
class UiDragSource {
public:
    virtual ~UiDragSource() = default;

    [[nodiscard]] virtual std::optional<UiDragPayload> begin_drag(
        const UiDragStartContext& context) const = 0;
    virtual void on_drag_event(const UiDragEvent& event) = 0;
};

class UiDropTarget {
public:
    virtual ~UiDropTarget() = default;

    [[nodiscard]] virtual bool accepts_drop(const UiDragPayload& payload) const = 0;
    virtual void on_drag_event(const UiDragEvent& event) = 0;
};

// Runtime-owned value state for one active pointer drag. It deliberately
// exposes IDs and payload only, so callers can render a preview or issue a
// command without gaining ownership of the retained source/target views.
class UiDragSession final {
public:
    [[nodiscard]] const std::string& source_root_id() const { return source_root_id_; }
    [[nodiscard]] const std::string& source_view_id() const { return source_view_id_; }
    [[nodiscard]] const std::string& hovered_root_id() const { return hovered_root_id_; }
    [[nodiscard]] const std::string& hovered_view_id() const { return hovered_view_id_; }
    [[nodiscard]] const UiDragPayload& payload() const { return payload_; }
    [[nodiscard]] UiPointerButton pointer_button() const { return pointer_button_; }

private:
    friend class UiInputRouter;

    std::string source_root_id_;
    std::string source_view_id_;
    std::string hovered_root_id_;
    std::string hovered_view_id_;
    UiDragPayload payload_{};
    UiPointerButton pointer_button_ = UiPointerButton::Primary;
};
class SlotView : public View, public UiDragSource, public UiDropTarget {
public:
    struct SlotState {
        std::string item_key;
        int32_t count = 0;
        bool selected = false;
    };
    using DragHandler = std::function<void(const UiDragEvent&)>;

    explicit SlotView(std::string id = {});
    ViewKind kind() const override { return ViewKind::SlotView; }

    void set_slot_state(SlotState state) {
        if (state_.item_key == state.item_key && state_.count == state.count &&
            state_.selected == state.selected) {
            return;
        }
        state_ = std::move(state);
    }
    const SlotState& slot_state() const { return state_; }
    void set_drag_handler(DragHandler handler) { drag_handler_ = std::move(handler); }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    [[nodiscard]] std::optional<UiDragPayload> begin_drag(
        const UiDragStartContext& context) const override;
    [[nodiscard]] bool accepts_drop(const UiDragPayload& payload) const override;
    void on_drag_event(const UiDragEvent& event) override;

private:
    SlotState state_{};
    DragHandler drag_handler_;
    bool drag_source_ = false;
    bool drag_hovered_ = false;
};

class ViewGroup : public View {
public:
    explicit ViewGroup(std::string id = {});
    ViewKind kind() const override { return ViewKind::ViewGroup; }

    View& add_child(std::unique_ptr<View> child);
    const std::vector<std::unique_ptr<View>>& children() const { return children_; }
    std::vector<std::unique_ptr<View>>& children() { return children_; }
    void clear_children();
    View* find(std::string_view id);
    const View* find(std::string_view id) const;

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;

protected:
    std::vector<std::unique_ptr<View>> children_;
};

class FlexLayout : public ViewGroup {
public:
    explicit FlexLayout(std::string id = {});
    ViewKind kind() const override { return ViewKind::FlexLayout; }

    void set_orientation(Orientation orientation) {
        if (orientation_ == orientation) return;
        orientation_ = orientation;
        mark_layout_dirty();
    }
    Orientation orientation() const { return orientation_; }
    void set_justify(FlexJustify justify) {
        if (justify_ == justify) return;
        justify_ = justify;
        mark_layout_dirty();
    }
    FlexJustify justify() const { return justify_; }
    void set_align(FlexAlign align) {
        if (align_ == align) return;
        align_ = align;
        mark_layout_dirty();
    }
    FlexAlign align() const { return align_; }
    void set_spacing(float spacing) {
        if (spacing_ == spacing) return;
        spacing_ = spacing;
        mark_layout_dirty();
    }
    float spacing() const { return spacing_; }
    void set_padding(Insets padding) {
        padding_ = padding;
        mark_layout_dirty();
    }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;

private:
    Orientation orientation_ = Orientation::Vertical;
    FlexJustify justify_ = FlexJustify::Start;
    FlexAlign align_ = FlexAlign::Stretch;
    float spacing_ = 0.0f;
    Insets padding_{};
};

// Fixed-column grid for inventory slots, recipe cells, and compact mod
// panels. It measures each visible child once and derives per-column/per-row
// cell extents; ScrollView owns viewport clipping and scroll offsets.
class GridLayout : public ViewGroup {
public:
    explicit GridLayout(std::string id = {});
    ViewKind kind() const override { return ViewKind::GridLayout; }

    void set_columns(int32_t columns) {
        const int32_t value = std::max(1, columns);
        if (columns_ == value) return;
        columns_ = value;
        mark_layout_dirty();
    }
    int32_t columns() const { return columns_; }
    void set_column_spacing(float spacing) {
        const float value = std::max(0.0f, spacing);
        if (column_spacing_ == value) return;
        column_spacing_ = value;
        mark_layout_dirty();
    }
    float column_spacing() const { return column_spacing_; }
    void set_row_spacing(float spacing) {
        const float value = std::max(0.0f, spacing);
        if (row_spacing_ == value) return;
        row_spacing_ = value;
        mark_layout_dirty();
    }
    float row_spacing() const { return row_spacing_; }
    void set_padding(Insets padding) {
        padding_ = padding;
        mark_layout_dirty();
    }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;

private:
    int32_t columns_ = 1;
    float column_spacing_ = 0.0f;
    float row_spacing_ = 0.0f;
    Insets padding_{};
};

class FrameLayout : public ViewGroup {
public:
    explicit FrameLayout(std::string id = {});
    ViewKind kind() const override { return ViewKind::FrameLayout; }

    void set_padding(Insets padding) {
        padding_ = padding;
        mark_layout_dirty();
    }
    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;

private:
    Insets padding_{};
};

enum class ScrollAxis : uint8_t {
    Vertical,
    Horizontal,
    Both,
};

// Single-content viewport with nested Arc2D clipping and wheel scrolling.
// The content remains a normal retained View, so GridLayout and custom mod
// views do not need a scroll-specific rendering or input implementation.
class ScrollView : public ViewGroup {
public:
    explicit ScrollView(std::string id = {});
    ViewKind kind() const override { return ViewKind::ScrollView; }

    View& set_content(std::unique_ptr<View> content);
    View* content();
    const View* content() const;

    void set_scroll_axis(ScrollAxis axis);
    ScrollAxis scroll_axis() const { return scroll_axis_; }
    void set_scroll_step(float pixels);
    float scroll_step() const { return scroll_step_; }
    void set_scroll_offset(Vec2 offset);
    Vec2 scroll_offset() const { return scroll_offset_; }
    Vec2 max_scroll_offset() const { return max_scroll_offset_; }
    Vec2 content_size() const { return content_size_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;
    bool accepts_child_input(Vec2 point) const override;

private:
    void layout_content();
    void clamp_scroll_offset();
    bool scroll_by(Vec2 delta);

    ScrollAxis scroll_axis_ = ScrollAxis::Vertical;
    float scroll_step_ = 36.0f;
    Vec2 content_size_{};
    Vec2 scroll_offset_{};
    Vec2 max_scroll_offset_{};
};

// A fixed-extent list that retains only visible item views plus a small
// overscan range. Item construction is internal to the UI layer; callers use
// stable indices and never retain a raw child pointer across a scroll.
class VirtualListView final : public ViewGroup {
public:
    using ItemBuilder = std::function<std::unique_ptr<View>(size_t index)>;

    explicit VirtualListView(std::string id = {});
    ViewKind kind() const override { return ViewKind::VirtualList; }

    void set_item_count(size_t count);
    size_t item_count() const { return item_count_; }
    void set_item_extent(float extent);
    float item_extent() const { return item_extent_; }
    void set_item_builder(ItemBuilder builder);
    void set_scroll_offset(float offset);
    float scroll_offset() const { return scroll_offset_; }
    float max_scroll_offset() const { return max_scroll_offset_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;
    bool accepts_child_input(Vec2 point) const override;

private:
    void realize_visible(TextEngine& text_engine, float available_width);
    void clamp_scroll_offset();
    bool scroll_by(float delta);

    ItemBuilder item_builder_;
    size_t item_count_ = 0;
    size_t first_realized_ = 0;
    float item_extent_ = 32.0f;
    float viewport_height_ = 0.0f;
    float scroll_offset_ = 0.0f;
    float max_scroll_offset_ = 0.0f;
    float scroll_step_ = 36.0f;
};

// Full-viewport modal surface. UiLayerStack supplies the blocking policy;
// this view owns the visual backdrop and optional backdrop-dismiss command.
class ModalView final : public FrameLayout {
public:
    using DismissHandler = std::function<void()>;

    explicit ModalView(std::string id = {});
    ViewKind kind() const override { return ViewKind::Modal; }

    void set_backdrop(Color color) { backdrop_ = color; }
    void set_dismiss_on_backdrop(bool value) { dismiss_on_backdrop_ = value; }
    void set_on_dismiss(DismissHandler handler) { dismiss_handler_ = std::move(handler); }

    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;

private:
    Color backdrop_{0, 0, 0, 150};
    bool dismiss_on_backdrop_ = false;
    DismissHandler dismiss_handler_;
};

class TooltipView final : public TextView {
public:
    explicit TooltipView(std::string id = {});
    ViewKind kind() const override { return ViewKind::Tooltip; }
};

class Animation {
public:
    using Setter = std::function<void(float)>;

    Animation(float from, float to, float duration_s, Setter setter);
    bool tick(float dt);
    bool finished() const { return finished_; }

private:
    float from_ = 0.0f;
    float to_ = 0.0f;
    float duration_s_ = 0.0f;
    float elapsed_s_ = 0.0f;
    Setter setter_;
    bool finished_ = false;
};

class Animator {
public:
    void add(Animation animation);
    void update(float dt);
    bool empty() const { return animations_.empty(); }

private:
    std::vector<Animation> animations_;
};

// Extension boundary for game-owned and mod-owned retained screens. A screen
// factory runs only while a screen is first mounted, never once per frame.
// Its root remains owned by UiLayerStack until close/unregister. Resource
// scenes use the same path as native game screens, but only the game-facing
// side ever sees a View pointer or a callable updater.

}  // namespace snt::ui
