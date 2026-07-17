// Stable Mod UI facade.
//
// This is the only UI API intended for content packages and script bridges.
// It carries value objects exclusively: no retained View, renderer, window,
// ECS, asset-manager, or native callback pointer can cross this boundary.

#pragma once

#include "core/expected.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace snt::ui::mod {

struct OwnerId {
    std::string value;
};

struct ScreenId {
    std::string value;
};

struct ViewModelId {
    std::string value;
};

struct ResourceRef {
    std::string value;
};

struct WidgetId {
    std::string value;
};

struct SlotState {
    ResourceRef item;
    int32_t count = 0;
    bool selected = false;
};

using Value = std::variant<std::monostate, bool, int64_t, double, std::string>;

// The host fills screen/widget identity before dispatch. related_widget and
// slot carry the opposite endpoint and item payload for slot drag events.
struct Command {
    std::string name;
    ScreenId screen;
    WidgetId widget;
    WidgetId related_widget;
    Value value;
    SlotState slot{};
};

// All user interaction leaves the facade as a typed command. Empty command
// names are intentionally inert, so declarative widgets can omit actions.
struct WidgetActions {
    Command activate;
    Command change;
    Command submit;
    Command dismiss;
    Command drag_begin;
    Command drag_enter;
    Command drag_leave;
    Command drop;
    Command drag_cancel;
};

enum class Layer : uint8_t {
    Hud,
    Screen,
    Modal,
    Tooltip,
};

enum class WidgetType : uint8_t {
    View,
    Flex,
    Grid,
    Frame,
    Scroll,
    Text,
    Button,
    Image,
    NineSlice,
    TextInput,
    TextEditor,
    Checkbox,
    Slider,
    VirtualList,
    Modal,
    Tooltip,
    Slot,
};

enum class Orientation : uint8_t {
    Horizontal,
    Vertical,
};

enum class FlexJustify : uint8_t {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

enum class FlexAlign : uint8_t {
    Start,
    Center,
    End,
    Stretch,
};

enum class ScrollAxis : uint8_t {
    Vertical,
    Horizontal,
    Both,
};

enum class TooltipPlacement : uint8_t {
    Auto,
    Top,
    Bottom,
    Left,
    Right,
};

// Tooltip belongs to its anchor widget. The runtime supplies the retained
// popup, delay, viewport clamping, and non-interactive Tooltip layer.
struct TooltipConfig {
    std::string text;
    float delay_seconds = 0.45f;
    float offset = 8.0f;
    TooltipPlacement placement = TooltipPlacement::Auto;
};

struct Insets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

struct Layout {
    // Negative means wrap-content, zero means fill the available axis, and
    // positive values are logical UI units. These values never use pixels.
    float width = -1.0f;
    float height = -1.0f;
    float weight = 0.0f;
    Insets margin{};
    Insets padding{};
    Orientation orientation = Orientation::Vertical;
    FlexJustify justify = FlexJustify::Start;
    FlexAlign align = FlexAlign::Stretch;
    float spacing = 0.0f;
    int32_t columns = 1;
    float column_spacing = 0.0f;
    float row_spacing = 0.0f;
    ScrollAxis scroll_axis = ScrollAxis::Vertical;
    float scroll_step = 36.0f;
};

// Declarative widget representation. actions emits typed commands; a
// view_model plus value_key binds a mutable widget value to a namespaced
// store owned by the facade. tooltip is declared on its anchor widget;
// VirtualList accepts at most one child template and realizes only visible
// indexed instances internally with variable measured row heights.
struct Widget {
    WidgetType type = WidgetType::View;
    WidgetId id;
    Layout layout{};
    bool visible = true;
    bool enabled = true;
    std::optional<bool> hit_test_visible;
    std::optional<bool> focusable;
    Color background{};
    bool has_background = false;
    float background_radius = 0.0f;
    std::string text;
    std::string placeholder;
    ResourceRef resource;
    Insets nine_slice_borders{};
    Color tint{};
    bool checked = false;
    bool password = false;
    size_t max_text_bytes = 4096;
    uint32_t min_text_lines = 3;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float step = 0.0f;
    float value = 0.0f;
    int32_t virtual_item_count = 0;
    float virtual_item_estimate = 32.0f;
    std::optional<TooltipConfig> tooltip;
    SlotState slot{};
    Color modal_backdrop{0, 0, 0, 150};
    bool dismiss_on_backdrop = false;
    WidgetActions actions{};
    ViewModelId view_model{};
    std::string value_key;
    std::vector<Widget> children;
};

struct Screen {
    ScreenId id;
    Layer layer = Layer::Screen;
    bool initially_visible = false;
    Widget root;
};

// The facade accepts decoded bytes rather than paths, file handles, or GPU
// objects. The host owns validation, atlas allocation, and eventual release.
struct ImageResource {
    ResourceRef ref;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

class IModUiCommandSink {
public:
    virtual ~IModUiCommandSink() = default;

    virtual snt::core::Expected<void> dispatch(Command command) = 0;
};

class IModUiHost {
public:
    virtual ~IModUiHost() = default;

    virtual const OwnerId& owner() const noexcept = 0;
    virtual snt::core::Expected<void> replace_screens(std::vector<Screen> screens) = 0;
    virtual snt::core::Expected<void> set_screen_visible(ScreenId screen, bool visible) = 0;
    virtual snt::core::Expected<void> set_view_model_value(ViewModelId model,
                                                            std::string key,
                                                            Value value) = 0;
    virtual snt::core::Expected<void> register_image(ImageResource image) = 0;
    virtual snt::core::Expected<void> unregister_owner() = 0;
};

// Host-side lifetime gateway for one graphical client runtime. The game or
// package loader uses this to hand a Mod its IModUiHost; the Mod never
// receives the layer stack, image registry, renderer, ECS, or a borrowed
// native object. Shared ownership keeps the command endpoint alive until the
// UI contribution has been detached.
class IModUiRuntime {
public:
    virtual ~IModUiRuntime() = default;

    [[nodiscard]] virtual snt::core::Expected<std::shared_ptr<IModUiHost>> attach(
        OwnerId owner,
        std::shared_ptr<IModUiCommandSink> command_sink) = 0;
    // Detach is idempotent so unload and runtime shutdown can both invoke it.
    virtual snt::core::Expected<void> detach(OwnerId owner) = 0;
    [[nodiscard]] virtual bool is_attached(const OwnerId& owner) const noexcept = 0;
    virtual void detach_all() noexcept = 0;
};

}  // namespace snt::ui::mod
