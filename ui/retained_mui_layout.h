// Retained-MUI containers, layout primitives, and viewport surfaces.

#pragma once

#include "retained_mui_types.h"
#include "retained_mui_view.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace snt::ui {

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

// A variable-height list that retains only visible item views plus a small
// overscan range. It caches measured row heights and can accept an exact
// height provider when data already knows each item's size. Item construction
// remains internal; callers use stable indices and never retain child views.
class VirtualListView final : public ViewGroup {
public:
    using ItemBuilder = std::function<std::unique_ptr<View>(size_t index)>;
    using ItemHeightProvider = std::function<float(size_t index)>;

    explicit VirtualListView(std::string id = {});
    ViewKind kind() const override { return ViewKind::VirtualList; }

    void set_item_count(size_t count);
    size_t item_count() const { return item_count_; }
    void set_item_estimate(float height);
    float item_estimate() const { return item_estimate_; }
    // When supplied, this is the exact logical height for each row. Without
    // it, realized rows measure themselves and unreached rows use estimate.
    void set_item_height_provider(ItemHeightProvider provider);
    void invalidate_item_heights();
    void set_item_builder(ItemBuilder builder);
    void set_scroll_offset(float offset);
    float scroll_offset() const { return scroll_offset_; }
    float max_scroll_offset() const { return max_scroll_offset_; }
    float content_height() const { return item_offsets_.empty() ? 0.0f : item_offsets_.back(); }
    size_t first_realized_index() const { return realized_begin_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;
    bool accepts_child_input(Vec2 point) const override;

private:
    void reset_item_heights();
    bool refresh_item_heights_from_provider();
    void rebuild_item_offsets();
    void refresh_scroll_limits();
    size_t item_index_at_offset(float offset) const;
    bool realize_visible(TextEngine& text_engine, float available_width);
    void layout_realized_children();
    void clamp_scroll_offset();
    bool scroll_by(float delta);

    ItemBuilder item_builder_;
    ItemHeightProvider item_height_provider_;
    size_t item_count_ = 0;
    size_t realized_begin_ = 0;
    size_t realized_end_ = 0;
    std::vector<size_t> realized_indices_;
    std::vector<float> item_heights_;
    std::vector<float> item_offsets_;
    float item_estimate_ = 32.0f;
    float measured_item_width_ = -1.0f;
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

}  // namespace snt::ui
