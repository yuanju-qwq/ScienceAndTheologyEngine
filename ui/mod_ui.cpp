// Internal retained-MUI implementation of the value-only Mod UI facade.
//
// Mods submit immutable screen declarations and update value models through
// IModUiHost. This file is the only place where those declarations become
// retained Views, so renderer/window/ECS handles cannot leak across the API.

#define SNT_LOG_CHANNEL "mod_ui"
#include "ui/mod_ui_internal.h"

#include "core/log.h"
#include "ui/retained_mui.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace snt::ui::mod::internal {
namespace {

constexpr size_t kMaximumWidgetDepth = 64;
constexpr size_t kMaximumWidgetCount = 4096;

using snt::core::Error;
using snt::core::ErrorCode;
using snt::core::Expected;

struct HostState {
    OwnerId owner;
    UiLayerStack* layers = nullptr;
    UiImageRegistry* images = nullptr;
    IModUiCommandSink* command_sink = nullptr;
    std::unordered_map<std::string, ViewModel> view_models;
    std::unordered_set<std::string> image_keys;
    bool active = true;
};

[[nodiscard]] Expected<void> invalid_argument(std::string message) {
    return Error{ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] Expected<void> invalid_state(std::string message) {
    return Error{ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool valid_scope_key(std::string_view value) {
    return !value.empty() && value.find(':') == std::string_view::npos;
}

[[nodiscard]] bool finite(float value) {
    return std::isfinite(value);
}

[[nodiscard]] bool valid_insets(const Insets& value) {
    return finite(value.left) && finite(value.top) && finite(value.right) &&
           finite(value.bottom) && value.left >= 0.0f && value.top >= 0.0f &&
           value.right >= 0.0f && value.bottom >= 0.0f;
}

[[nodiscard]] Expected<void> validate_value(const Value& value,
                                             std::string_view path) {
    if (const auto* number = std::get_if<double>(&value);
        number && !std::isfinite(*number)) {
        return invalid_argument(std::string(path) + " must not contain a non-finite number");
    }
    return {};
}

[[nodiscard]] Expected<void> validate_command(const Command& command,
                                               std::string_view path) {
    if (auto result = validate_value(command.value, std::string(path) + ".value"); !result) {
        return result.error();
    }
    if (command.slot.count < 0) {
        return invalid_argument(std::string(path) + ".slot.count must not be negative");
    }
    if (!command.slot.item.value.empty() && !valid_scope_key(command.slot.item.value)) {
        return invalid_argument(std::string(path) + ".slot.item is invalid");
    }
    return {};
}

[[nodiscard]] Expected<void> validate_actions(const WidgetActions& actions,
                                               std::string_view path) {
    const std::pair<std::string_view, const Command*> entries[] = {
        {"activate", &actions.activate}, {"change", &actions.change},
        {"submit", &actions.submit}, {"dismiss", &actions.dismiss},
        {"drag_begin", &actions.drag_begin}, {"drag_enter", &actions.drag_enter},
        {"drag_leave", &actions.drag_leave}, {"drop", &actions.drop},
        {"drag_cancel", &actions.drag_cancel},
    };
    for (const auto& [name, command] : entries) {
        if (auto result = validate_command(*command, std::string(path) + "." +
                                           std::string(name)); !result) {
            return result.error();
        }
    }
    return {};
}

[[nodiscard]] Expected<void> validate_widget(const Widget& widget,
                                              std::string path,
                                              size_t depth,
                                              size_t& count,
                                              std::unordered_set<std::string>& ids) {
    if (depth > kMaximumWidgetDepth) {
        return invalid_argument(path + " exceeds the maximum widget depth");
    }
    if (++count > kMaximumWidgetCount) {
        return invalid_argument("Mod UI screen exceeds the maximum widget count");
    }
    if (widget.id.value.empty()) {
        return invalid_argument(path + ".id must not be empty");
    }
    if (!ids.emplace(widget.id.value).second) {
        return invalid_argument("Mod UI screen contains duplicate widget id: " + widget.id.value);
    }

    const Layout& layout = widget.layout;
    if (!finite(layout.width) || !finite(layout.height) || !finite(layout.weight) ||
        layout.weight < 0.0f || !valid_insets(layout.margin) || !valid_insets(layout.padding) ||
        !finite(layout.spacing) || layout.spacing < 0.0f || layout.columns < 1 ||
        !finite(layout.column_spacing) || layout.column_spacing < 0.0f ||
        !finite(layout.row_spacing) || layout.row_spacing < 0.0f ||
        !finite(layout.scroll_step) || layout.scroll_step <= 0.0f) {
        return invalid_argument(path + " has invalid layout values");
    }
    if (!finite(widget.background_radius) || widget.background_radius < 0.0f ||
        !finite(widget.minimum) || !finite(widget.maximum) || widget.maximum < widget.minimum ||
        !finite(widget.step) || widget.step < 0.0f || !finite(widget.value) ||
        widget.virtual_item_count < 0 || !finite(widget.virtual_item_extent) ||
        widget.virtual_item_extent <= 0.0f || widget.max_text_bytes == 0 ||
        widget.slot.count < 0 || !valid_insets(widget.nine_slice_borders)) {
        return invalid_argument(path + " has invalid control values");
    }
    if ((widget.nine_slice_borders.left < 0.0f || widget.nine_slice_borders.top < 0.0f ||
         widget.nine_slice_borders.right < 0.0f || widget.nine_slice_borders.bottom < 0.0f)) {
        return invalid_argument(path + ".nine_slice_borders must be non-negative");
    }
    if (!widget.slot.item.value.empty() && !valid_scope_key(widget.slot.item.value)) {
        return invalid_argument(path + ".slot.item is invalid");
    }
    if (widget.view_model.value.empty() != widget.value_key.empty()) {
        return invalid_argument(path + " requires both view_model and value_key together");
    }
    if (!widget.view_model.value.empty() && !valid_scope_key(widget.view_model.value)) {
        return invalid_argument(path + ".view_model is invalid");
    }
    if (auto result = validate_actions(widget.actions, path + ".actions"); !result) {
        return result.error();
    }

    const bool image_widget = widget.type == WidgetType::Image ||
                              widget.type == WidgetType::NineSlice;
    if (image_widget && !valid_scope_key(widget.resource.value)) {
        return invalid_argument(path + ".resource is required for image widgets");
    }
    if (!image_widget && !widget.resource.value.empty()) {
        return invalid_argument(path + ".resource is only valid on image widgets");
    }
    if (widget.type != WidgetType::NineSlice &&
        (widget.nine_slice_borders.left != 0.0f || widget.nine_slice_borders.top != 0.0f ||
         widget.nine_slice_borders.right != 0.0f || widget.nine_slice_borders.bottom != 0.0f)) {
        return invalid_argument(path + ".nine_slice_borders are only valid on NineSlice");
    }

    const bool leaf = widget.type == WidgetType::View || widget.type == WidgetType::Text ||
                      widget.type == WidgetType::Button || widget.type == WidgetType::Image ||
                      widget.type == WidgetType::NineSlice ||
                      widget.type == WidgetType::TextInput ||
                      widget.type == WidgetType::Checkbox || widget.type == WidgetType::Slider ||
                      widget.type == WidgetType::Tooltip || widget.type == WidgetType::Slot;
    if (leaf && !widget.children.empty()) {
        return invalid_argument(path + " is a leaf widget and cannot contain children");
    }
    if ((widget.type == WidgetType::Scroll || widget.type == WidgetType::VirtualList) &&
        widget.children.size() > 1) {
        return invalid_argument(path + " accepts at most one child template");
    }

    for (size_t index = 0; index < widget.children.size(); ++index) {
        if (auto result = validate_widget(widget.children[index],
                                          path + ".children[" + std::to_string(index) + "]",
                                          depth + 1, count, ids); !result) {
            return result.error();
        }
    }
    return {};
}

[[nodiscard]] Expected<void> validate_screen(const Screen& screen) {
    if (!valid_scope_key(screen.id.value)) {
        return invalid_argument("Mod UI screen ID is required and cannot contain ':'");
    }
    size_t count = 0;
    std::unordered_set<std::string> ids;
    return validate_widget(screen.root, "screen(" + screen.id.value + ").root", 0, count, ids);
}

[[nodiscard]] snt::ui::Insets to_ui_insets(const Insets& value) {
    return {.left = value.left, .top = value.top, .right = value.right, .bottom = value.bottom};
}

[[nodiscard]] snt::ui::Color to_ui_color(const Color& value) {
    return {.r = value.r, .g = value.g, .b = value.b, .a = value.a};
}

[[nodiscard]] snt::ui::Orientation to_ui_orientation(Orientation value) {
    return value == Orientation::Horizontal ? snt::ui::Orientation::Horizontal
                                            : snt::ui::Orientation::Vertical;
}

[[nodiscard]] snt::ui::FlexJustify to_ui_justify(FlexJustify value) {
    switch (value) {
    case FlexJustify::Start: return snt::ui::FlexJustify::Start;
    case FlexJustify::Center: return snt::ui::FlexJustify::Center;
    case FlexJustify::End: return snt::ui::FlexJustify::End;
    case FlexJustify::SpaceBetween: return snt::ui::FlexJustify::SpaceBetween;
    case FlexJustify::SpaceAround: return snt::ui::FlexJustify::SpaceAround;
    case FlexJustify::SpaceEvenly: return snt::ui::FlexJustify::SpaceEvenly;
    }
    return snt::ui::FlexJustify::Start;
}

[[nodiscard]] snt::ui::FlexAlign to_ui_align(FlexAlign value) {
    switch (value) {
    case FlexAlign::Start: return snt::ui::FlexAlign::Start;
    case FlexAlign::Center: return snt::ui::FlexAlign::Center;
    case FlexAlign::End: return snt::ui::FlexAlign::End;
    case FlexAlign::Stretch: return snt::ui::FlexAlign::Stretch;
    }
    return snt::ui::FlexAlign::Stretch;
}

[[nodiscard]] snt::ui::ScrollAxis to_ui_scroll_axis(ScrollAxis value) {
    switch (value) {
    case ScrollAxis::Vertical: return snt::ui::ScrollAxis::Vertical;
    case ScrollAxis::Horizontal: return snt::ui::ScrollAxis::Horizontal;
    case ScrollAxis::Both: return snt::ui::ScrollAxis::Both;
    }
    return snt::ui::ScrollAxis::Vertical;
}

[[nodiscard]] snt::ui::UiLayer to_ui_layer(Layer value) {
    switch (value) {
    case Layer::Hud: return snt::ui::UiLayer::Hud;
    case Layer::Screen: return snt::ui::UiLayer::Screen;
    case Layer::Modal: return snt::ui::UiLayer::Modal;
    case Layer::Tooltip: return snt::ui::UiLayer::Tooltip;
    }
    return snt::ui::UiLayer::Screen;
}

[[nodiscard]] snt::ui::LayoutParams to_ui_layout_params(const Layout& layout) {
    return {.width = layout.width,
            .height = layout.height,
            .weight = layout.weight,
            .margin = to_ui_insets(layout.margin)};
}

[[nodiscard]] std::string image_key(const HostState& state, const ResourceRef& resource) {
    return state.owner.value + ":" + resource.value;
}

[[nodiscard]] ResourceRef local_resource_ref(const HostState& state, std::string_view key) {
    const std::string prefix = state.owner.value + ":";
    if (key.starts_with(prefix)) return {.value = std::string(key.substr(prefix.size()))};
    return {.value = std::string(key)};
}

[[nodiscard]] SlotState to_mod_slot(const HostState& state,
                                    const snt::ui::SlotView::SlotState& slot) {
    return {.item = local_resource_ref(state, slot.item_key),
            .count = slot.count,
            .selected = slot.selected};
}

void apply_common(snt::ui::View& view, const Widget& widget) {
    view.set_layout_params(to_ui_layout_params(widget.layout));
    view.set_visibility(widget.visible ? snt::ui::Visibility::Visible
                                      : snt::ui::Visibility::Gone);
    view.set_enabled(widget.enabled);
    if (widget.hit_test_visible) view.set_hit_test_visible(*widget.hit_test_visible);
    if (widget.focusable) view.set_focusable(*widget.focusable);
    if (widget.has_background) {
        view.set_background(to_ui_color(widget.background), widget.background_radius);
    }
}

void set_model_value(const std::weak_ptr<HostState>& weak_state,
                     const std::string& model_id,
                     const std::string& key,
                     BindingValue value) {
    if (model_id.empty() || key.empty()) return;
    if (const auto state = weak_state.lock(); state && state->active) {
        state->view_models[model_id].set(key, std::move(value));
    }
}

void dispatch_command(const std::weak_ptr<HostState>& weak_state,
                      Command declaration,
                      ScreenId screen,
                      WidgetId widget,
                      std::optional<Value> dynamic_value = std::nullopt,
                      WidgetId related_widget = {},
                      std::optional<SlotState> slot = std::nullopt) {
    if (declaration.name.empty()) return;
    const auto state = weak_state.lock();
    if (!state || !state->active || !state->command_sink) return;

    declaration.screen = std::move(screen);
    declaration.widget = std::move(widget);
    declaration.related_widget = std::move(related_widget);
    if (dynamic_value) declaration.value = std::move(*dynamic_value);
    if (slot) declaration.slot = std::move(*slot);

    if (auto result = state->command_sink->dispatch(std::move(declaration)); !result) {
        SNT_LOG_WARN("Mod UI command dispatch failed for owner='%s': %s",
                     state->owner.value.c_str(), result.error().format().c_str());
    }
}

void dispatch_slot_command(const std::weak_ptr<HostState>& weak_state,
                           const ScreenId& screen,
                           const WidgetId& widget,
                           const WidgetActions& actions,
                           const UiSlotDragEvent& event) {
    const Command* declaration = nullptr;
    switch (event.type) {
    case UiSlotDragEventType::Begin: declaration = &actions.drag_begin; break;
    case UiSlotDragEventType::Enter: declaration = &actions.drag_enter; break;
    case UiSlotDragEventType::Leave: declaration = &actions.drag_leave; break;
    case UiSlotDragEventType::Drop: declaration = &actions.drop; break;
    case UiSlotDragEventType::Cancel: declaration = &actions.drag_cancel; break;
    }
    if (!declaration || declaration->name.empty()) return;

    const auto state = weak_state.lock();
    if (!state || !state->active) return;
    const std::string& other_id = event.source_id == widget.value
        ? event.target_id : event.source_id;
    dispatch_command(weak_state, *declaration, screen, widget, std::nullopt,
                     {.value = other_id}, to_mod_slot(*state, event.payload));
}

void bind_text_value(snt::ui::TextView& view,
                     HostState& state,
                     const Widget& widget) {
    if (widget.view_model.value.empty()) return;
    auto& model = state.view_models[widget.view_model.value];
    auto* raw = &view;
    view.bind_value(model, widget.value_key,
                    [raw](std::string_view, const BindingValue& value) {
                        if (const auto* text = std::get_if<std::string>(&value)) {
                            raw->set_text(*text);
                        }
                    });
}

void bind_text_input(snt::ui::TextInput& view,
                     HostState& state,
                     const Widget& widget,
                     const ScreenId& screen,
                     const std::weak_ptr<HostState>& weak_state) {
    const std::string model_id = widget.view_model.value;
    const std::string value_key = widget.value_key;
    const WidgetId widget_id = widget.id;
    const Command change = widget.actions.change;
    const Command submit = widget.actions.submit;
    view.set_on_change([weak_state, model_id, value_key, screen, widget_id, change]
                       (std::string_view value) {
        const std::string text(value);
        set_model_value(weak_state, model_id, value_key, BindingValue{text});
        dispatch_command(weak_state, change, screen, widget_id, Value{text});
    });
    view.set_on_submit([weak_state, screen, widget_id, submit](std::string_view value) {
        dispatch_command(weak_state, submit, screen, widget_id, Value{std::string(value)});
    });
    if (model_id.empty()) return;
    auto& model = state.view_models[model_id];
    auto* raw = &view;
    view.bind_value(model, value_key,
                    [raw](std::string_view, const BindingValue& value) {
                        if (const auto* text = std::get_if<std::string>(&value)) {
                            raw->set_text_silently(*text);
                        }
                    });
}

void bind_checkbox(snt::ui::Checkbox& view,
                   HostState& state,
                   const Widget& widget,
                   const ScreenId& screen,
                   const std::weak_ptr<HostState>& weak_state) {
    const std::string model_id = widget.view_model.value;
    const std::string value_key = widget.value_key;
    const WidgetId widget_id = widget.id;
    const Command change = widget.actions.change;
    view.set_on_change([weak_state, model_id, value_key, screen, widget_id, change](bool value) {
        set_model_value(weak_state, model_id, value_key, BindingValue{value});
        dispatch_command(weak_state, change, screen, widget_id, Value{value});
    });
    if (model_id.empty()) return;
    auto& model = state.view_models[model_id];
    auto* raw = &view;
    view.bind_value(model, value_key,
                    [raw](std::string_view, const BindingValue& value) {
                        if (const auto* checked = std::get_if<bool>(&value)) {
                            raw->set_checked(*checked);
                        }
                    });
}

void bind_slider(snt::ui::Slider& view,
                 HostState& state,
                 const Widget& widget,
                 const ScreenId& screen,
                 const std::weak_ptr<HostState>& weak_state) {
    const std::string model_id = widget.view_model.value;
    const std::string value_key = widget.value_key;
    const WidgetId widget_id = widget.id;
    const Command change = widget.actions.change;
    view.set_on_change([weak_state, model_id, value_key, screen, widget_id, change](float value) {
        set_model_value(weak_state, model_id, value_key, BindingValue{static_cast<double>(value)});
        dispatch_command(weak_state, change, screen, widget_id,
                         Value{static_cast<double>(value)});
    });
    if (model_id.empty()) return;
    auto& model = state.view_models[model_id];
    auto* raw = &view;
    view.bind_value(model, value_key,
                    [raw](std::string_view, const BindingValue& value) {
                        if (const auto* number = std::get_if<double>(&value)) {
                            raw->set_value(static_cast<float>(*number));
                        } else if (const auto* integer = std::get_if<int64_t>(&value)) {
                            raw->set_value(static_cast<float>(*integer));
                        }
                    });
}

[[nodiscard]] Expected<std::unique_ptr<snt::ui::View>> instantiate_widget(
    const std::shared_ptr<HostState>& state,
    const ScreenId& screen,
    const Widget& widget);

[[nodiscard]] Expected<void> add_children(snt::ui::ViewGroup& group,
                                          const std::shared_ptr<HostState>& state,
                                          const ScreenId& screen,
                                          const Widget& widget) {
    for (const Widget& child_widget : widget.children) {
        auto child = instantiate_widget(state, screen, child_widget);
        if (!child) return child.error();
        group.add_child(std::move(*child));
    }
    return {};
}

void prefix_widget_ids(Widget& widget, std::string_view prefix) {
    widget.id.value = std::string(prefix) + widget.id.value;
    for (Widget& child : widget.children) prefix_widget_ids(child, prefix);
}

[[nodiscard]] Expected<std::unique_ptr<snt::ui::View>> instantiate_widget(
    const std::shared_ptr<HostState>& state,
    const ScreenId& screen,
    const Widget& widget) {
    const std::weak_ptr<HostState> weak_state = state;
    switch (widget.type) {
    case WidgetType::View: {
        auto view = std::make_unique<snt::ui::View>(widget.id.value);
        apply_common(*view, widget);
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Flex: {
        auto view = std::make_unique<snt::ui::FlexLayout>(widget.id.value);
        apply_common(*view, widget);
        view->set_orientation(to_ui_orientation(widget.layout.orientation));
        view->set_justify(to_ui_justify(widget.layout.justify));
        view->set_align(to_ui_align(widget.layout.align));
        view->set_spacing(widget.layout.spacing);
        view->set_padding(to_ui_insets(widget.layout.padding));
        if (auto result = add_children(*view, state, screen, widget); !result) {
            return result.error();
        }
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Grid: {
        auto view = std::make_unique<snt::ui::GridLayout>(widget.id.value);
        apply_common(*view, widget);
        view->set_columns(widget.layout.columns);
        view->set_column_spacing(widget.layout.column_spacing);
        view->set_row_spacing(widget.layout.row_spacing);
        view->set_padding(to_ui_insets(widget.layout.padding));
        if (auto result = add_children(*view, state, screen, widget); !result) {
            return result.error();
        }
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Frame: {
        auto view = std::make_unique<snt::ui::FrameLayout>(widget.id.value);
        apply_common(*view, widget);
        view->set_padding(to_ui_insets(widget.layout.padding));
        if (auto result = add_children(*view, state, screen, widget); !result) {
            return result.error();
        }
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Scroll: {
        auto view = std::make_unique<snt::ui::ScrollView>(widget.id.value);
        apply_common(*view, widget);
        view->set_scroll_axis(to_ui_scroll_axis(widget.layout.scroll_axis));
        view->set_scroll_step(widget.layout.scroll_step);
        if (!widget.children.empty()) {
            auto content = instantiate_widget(state, screen, widget.children.front());
            if (!content) return content.error();
            view->set_content(std::move(*content));
        }
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Text: {
        auto view = std::make_unique<snt::ui::TextView>(widget.id.value);
        apply_common(*view, widget);
        view->set_text(widget.text);
        bind_text_value(*view, *state, widget);
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Button: {
        auto view = std::make_unique<snt::ui::Button>(widget.id.value);
        apply_common(*view, widget);
        view->set_text(widget.text);
        const Command activate = widget.actions.activate;
        const WidgetId widget_id = widget.id;
        view->set_on_activate([weak_state, screen, widget_id, activate] {
            dispatch_command(weak_state, activate, screen, widget_id);
        });
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Image: {
        auto view = std::make_unique<snt::ui::ImageView>(widget.id.value);
        apply_common(*view, widget);
        view->set_image_key(image_key(*state, widget.resource));
        view->set_tint(to_ui_color(widget.tint));
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::NineSlice: {
        auto view = std::make_unique<snt::ui::NineSliceView>(widget.id.value);
        apply_common(*view, widget);
        view->set_image_key(image_key(*state, widget.resource));
        view->set_borders(to_ui_insets(widget.nine_slice_borders));
        view->set_tint(to_ui_color(widget.tint));
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::TextInput: {
        auto view = std::make_unique<snt::ui::TextInput>(widget.id.value);
        apply_common(*view, widget);
        view->set_max_bytes(widget.max_text_bytes);
        view->set_password(widget.password);
        view->set_placeholder(widget.placeholder);
        view->set_text_silently(widget.text);
        bind_text_input(*view, *state, widget, screen, weak_state);
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Checkbox: {
        auto view = std::make_unique<snt::ui::Checkbox>(widget.id.value);
        apply_common(*view, widget);
        view->set_text(widget.text);
        view->set_checked(widget.checked);
        bind_checkbox(*view, *state, widget, screen, weak_state);
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Slider: {
        auto view = std::make_unique<snt::ui::Slider>(widget.id.value);
        apply_common(*view, widget);
        view->set_range(widget.minimum, widget.maximum);
        view->set_step(widget.step);
        view->set_value(widget.value);
        bind_slider(*view, *state, widget, screen, weak_state);
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::VirtualList: {
        auto view = std::make_unique<snt::ui::VirtualListView>(widget.id.value);
        apply_common(*view, widget);
        view->set_item_count(static_cast<size_t>(widget.virtual_item_count));
        view->set_item_extent(widget.virtual_item_extent);
        if (!widget.children.empty()) {
            const Widget item_template = widget.children.front();
            const std::string item_prefix = widget.id.value + "[";
            view->set_item_builder([weak_state, screen, item_template, item_prefix](size_t index)
                                   -> std::unique_ptr<snt::ui::View> {
                const auto retained_state = weak_state.lock();
                if (!retained_state || !retained_state->active) return nullptr;
                Widget item = item_template;
                const std::string prefix = item_prefix + std::to_string(index) + "]/";
                prefix_widget_ids(item, prefix);
                auto instantiated = instantiate_widget(retained_state, screen, item);
                if (!instantiated) {
                    SNT_LOG_ERROR("Mod UI virtual-list item mount failed: %s",
                                  instantiated.error().format().c_str());
                    return nullptr;
                }
                return std::move(*instantiated);
            });
        }
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Modal: {
        auto view = std::make_unique<snt::ui::ModalView>(widget.id.value);
        apply_common(*view, widget);
        view->set_padding(to_ui_insets(widget.layout.padding));
        view->set_backdrop(to_ui_color(widget.modal_backdrop));
        view->set_dismiss_on_backdrop(widget.dismiss_on_backdrop);
        const Command dismiss = widget.actions.dismiss;
        const WidgetId widget_id = widget.id;
        view->set_on_dismiss([weak_state, screen, widget_id, dismiss] {
            dispatch_command(weak_state, dismiss, screen, widget_id);
        });
        if (auto result = add_children(*view, state, screen, widget); !result) {
            return result.error();
        }
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Tooltip: {
        auto view = std::make_unique<snt::ui::TooltipView>(widget.id.value);
        apply_common(*view, widget);
        view->set_text(widget.text);
        bind_text_value(*view, *state, widget);
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    case WidgetType::Slot: {
        auto view = std::make_unique<snt::ui::SlotView>(widget.id.value);
        apply_common(*view, widget);
        view->set_slot_state({.item_key = widget.slot.item.value.empty()
                                              ? std::string{}
                                              : image_key(*state, widget.slot.item),
                              .count = widget.slot.count,
                              .selected = widget.slot.selected});
        const WidgetId widget_id = widget.id;
        const WidgetActions actions = widget.actions;
        view->set_drag_handler([weak_state, screen, widget_id, actions](
                                   const UiSlotDragEvent& event) {
            dispatch_slot_command(weak_state, screen, widget_id, actions, event);
        });
        return std::unique_ptr<snt::ui::View>(std::move(view));
    }
    }
    return invalid_argument("Mod UI contains an unsupported widget type").error();
}

class ModUiHost final : public IModUiHost {
public:
    explicit ModUiHost(std::shared_ptr<HostState> state)
        : state_(std::move(state)) {}

    ~ModUiHost() override {
        if (state_ && state_->active) {
            if (auto result = unregister_owner(); !result) {
                SNT_LOG_WARN("Mod UI host teardown failed for owner='%s': %s",
                             state_->owner.value.c_str(), result.error().format().c_str());
            }
        }
    }

    const OwnerId& owner() const noexcept override { return state_->owner; }

    Expected<void> replace_screens(std::vector<Screen> screens) override {
        if (!state_ || !state_->active) return invalid_state("Mod UI host is no longer active");

        std::unordered_set<std::string> ids;
        ids.reserve(screens.size());
        for (const Screen& screen : screens) {
            if (auto result = validate_screen(screen); !result) return result.error();
            if (!ids.emplace(screen.id.value).second) {
                return invalid_argument("Mod UI replacement contains duplicate screen IDs");
            }
        }

        std::vector<snt::ui::UiScreenRegistration> registrations;
        registrations.reserve(screens.size());
        const std::weak_ptr<HostState> weak_state = state_;
        for (Screen& screen : screens) {
            auto declaration = std::make_shared<Screen>(std::move(screen));
            registrations.push_back({
                .owner_id = state_->owner.value,
                .screen_id = declaration->id.value,
                .layer = to_ui_layer(declaration->layer),
                .initially_visible = declaration->initially_visible,
                .factory = [weak_state, declaration](const snt::ui::UiScreenMountContext&)
                    -> Expected<snt::ui::UiScreenMount> {
                    const auto retained_state = weak_state.lock();
                    if (!retained_state || !retained_state->active) {
                        return Error{ErrorCode::kInvalidState,
                                     "Mod UI screen factory outlived its host"};
                    }
                    auto root = instantiate_widget(retained_state, declaration->id,
                                                   declaration->root);
                    if (!root) return root.error();
                    return snt::ui::UiScreenMount{.root = std::move(*root)};
                },
            });
        }

        auto result = state_->layers->replace_owner_screens(state_->owner.value,
                                                              std::move(registrations));
        if (!result) {
            auto error = result.error();
            error.with_context("ModUiHost::replace_screens(" + state_->owner.value + ")");
            return error;
        }
        SNT_LOG_INFO("Mod UI screens replaced: owner='%s' screens=%zu",
                     state_->owner.value.c_str(), ids.size());
        return {};
    }

    Expected<void> set_screen_visible(ScreenId screen, bool visible) override {
        if (!state_ || !state_->active) return invalid_state("Mod UI host is no longer active");
        if (!valid_scope_key(screen.value)) return invalid_argument("Mod UI screen ID is invalid");
        auto result = state_->layers->set_visible(state_->owner.value, screen.value, visible);
        if (!result) {
            auto error = result.error();
            error.with_context("ModUiHost::set_screen_visible(" + screen.value + ")");
            return error;
        }
        return {};
    }

    Expected<void> set_view_model_value(ViewModelId model,
                                        std::string key,
                                        Value value) override {
        if (!state_ || !state_->active) return invalid_state("Mod UI host is no longer active");
        if (!valid_scope_key(model.value) || key.empty()) {
            return invalid_argument("Mod UI view-model ID and key are required");
        }
        if (auto result = validate_value(value, "Mod UI view-model value"); !result) {
            return result.error();
        }
        BindingValue bound_value = std::move(value);
        state_->view_models[model.value].set(std::move(key), std::move(bound_value));
        return {};
    }

    Expected<void> register_image(ImageResource image) override {
        if (!state_ || !state_->active) return invalid_state("Mod UI host is no longer active");
        if (!valid_scope_key(image.ref.value)) {
            return invalid_argument("Mod UI image resource reference is invalid");
        }
        const std::string key = image_key(*state_, image.ref);
        auto result = state_->images->register_rgba(key, image.width, image.height,
                                                    std::move(image.rgba));
        if (!result) {
            auto error = result.error();
            error.with_context("ModUiHost::register_image(" + image.ref.value + ")");
            return error;
        }
        state_->image_keys.emplace(key);
        return {};
    }

    Expected<void> unregister_owner() override {
        if (!state_ || !state_->active) return {};
        state_->active = false;
        (void)state_->layers->unregister_owner(state_->owner.value);

        std::optional<Error> first_error;
        for (const std::string& key : state_->image_keys) {
            if (auto result = state_->images->unregister_image(key); !result && !first_error) {
                first_error = result.error();
            }
        }
        const size_t image_count = state_->image_keys.size();
        state_->image_keys.clear();
        state_->view_models.clear();
        if (first_error) {
            first_error->with_context("ModUiHost::unregister_owner(" + state_->owner.value + ")");
            return std::move(*first_error);
        }
        SNT_LOG_INFO("Mod UI owner unregistered: owner='%s' images=%zu",
                     state_->owner.value.c_str(), image_count);
        return {};
    }

private:
    std::shared_ptr<HostState> state_;
};

}  // namespace

Expected<std::unique_ptr<IModUiHost>> create_mod_ui_host(OwnerId owner,
                                                          UiLayerStack& layers,
                                                          UiImageRegistry& images,
                                                          IModUiCommandSink& command_sink) {
    if (!valid_scope_key(owner.value)) {
        return Error{ErrorCode::kInvalidArgument,
                     "Mod UI owner ID is required and cannot contain ':'"};
    }
    auto state = std::make_shared<HostState>();
    state->owner = std::move(owner);
    state->layers = &layers;
    state->images = &images;
    state->command_sink = &command_sink;
    return std::unique_ptr<IModUiHost>(std::make_unique<ModUiHost>(std::move(state)));
}

}  // namespace snt::ui::mod::internal
