// Retained-MUI common values and host-neutral input contracts.

#pragma once

#include "core/expected.h"
#include "ui_draw_data.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace snt::ui {

enum class MeasureMode : uint8_t {
    Unspecified,
    AtMost,
    Exactly,
};

struct MeasureSpec {
    float size = 0.0f;
    MeasureMode mode = MeasureMode::Unspecified;
};

struct Insets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct LayoutParams {
    float width = -1.0f;   // <0 wrap content, 0 match parent, >0 exact px
    float height = -1.0f;
    float weight = 0.0f;
    Insets margin;
};

enum class Orientation : uint8_t {
    Horizontal,
    Vertical,
};

// One-axis flex alignment. FlexLayout deliberately does not implement wrap:
// dense game UI uses GridLayout for two-dimensional placement, while flex is
// the predictable row/column primitive used for panels and toolbars.
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

// Scroll direction is shared by retained containers and packed/mod scene
// descriptions, so it belongs to the common UI value contract.
enum class ScrollAxis : uint8_t {
    Vertical,
    Horizontal,
    Both,
};

// Maps one platform window to the UI coordinate system. `window_size` is in
// SDL window units, `framebuffer_size` is Vulkan pixel units, and all
// retained layout/input APIs use logical UI units. The final Arc2D output is
// multiplied by pixels_per_ui_unit() immediately before rendering.
struct UiViewport {
    Vec2 framebuffer_size{};
    Vec2 window_size{};
    float dpi_scale = 1.0f;
    float user_scale = 1.0f;

    [[nodiscard]] float pixels_per_ui_unit() const {
        return std::max(0.01f, dpi_scale) * std::max(0.01f, user_scale);
    }
    [[nodiscard]] Vec2 logical_size() const {
        const float scale = pixels_per_ui_unit();
        return {framebuffer_size.x / scale, framebuffer_size.y / scale};
    }
    [[nodiscard]] Vec2 window_to_logical(Vec2 point) const {
        const float framebuffer_x = window_size.x > 0.0f
            ? point.x * framebuffer_size.x / window_size.x : point.x;
        const float framebuffer_y = window_size.y > 0.0f
            ? point.y * framebuffer_size.y / window_size.y : point.y;
        const float scale = pixels_per_ui_unit();
        return {framebuffer_x / scale, framebuffer_y / scale};
    }
    [[nodiscard]] Vec2 logical_to_window(Vec2 point) const {
        const float scale = pixels_per_ui_unit();
        const float framebuffer_x = point.x * scale;
        const float framebuffer_y = point.y * scale;
        return {
            framebuffer_size.x > 0.0f ? framebuffer_x * window_size.x / framebuffer_size.x
                                      : framebuffer_x,
            framebuffer_size.y > 0.0f ? framebuffer_y * window_size.y / framebuffer_size.y
                                      : framebuffer_y,
        };
    }    [[nodiscard]] bool valid() const {
        return framebuffer_size.x > 0.0f && framebuffer_size.y > 0.0f &&
               window_size.x > 0.0f && window_size.y > 0.0f &&
               dpi_scale > 0.0f && user_scale > 0.0f;
    }
};

enum class Visibility : uint8_t {
    Visible,
    Hidden,
    Gone,
};

// UI layer order is shared by the host, game screens, and future mod UI.
// Higher layers render later and receive pointer input before lower layers.
// These four layers are the complete public layering contract. Engine debug
// overlays use Hud so Mods never need to depend on a private fifth layer.
enum class UiLayer : uint8_t {
    Hud,
    Screen,
    Modal,
    Tooltip,
};

// Layer ownership is centralized so rendering order and input behavior cannot
// drift between client hosts. Tooltip remains visual-only; a modal blocks
// lower layers even when its root does not hit-test the pointer.
struct UiLayerInputPolicy {
    bool accepts_pointer = true;
    bool accepts_keyboard = true;
    bool blocks_pointer_below = false;
    bool blocks_keyboard_below = false;
};

[[nodiscard]] UiLayerInputPolicy ui_layer_input_policy(UiLayer layer);

enum class UiPointerButton : uint8_t {
    None,
    Primary,
    Middle,
    Secondary,
};

// Platform hosts map their native keyboard values onto this small, stable
// navigation set. Raw SDL or platform key codes do not leak into mod UI APIs.
enum class UiKey : uint8_t {
    Unknown,
    Enter,
    Space,
    Escape,
    Tab,
    Backspace,
    Delete,
    Home,
    End,
    Left,
    Right,
    Up,
    Down,
    A,
    C,
    V,
    X,
    Y,
    Z,
};

// Modifier state is semantic rather than platform-specific. Hosts map their
// native shortcut state here so retained widgets and Mods never receive SDL
// key codes or SDL modifier masks.
enum class UiKeyModifier : uint8_t {
    None = 0,
    Shift = 1u << 0u,
    Control = 1u << 1u,
    Alt = 1u << 2u,
    Meta = 1u << 3u,
};

constexpr UiKeyModifier operator|(UiKeyModifier left, UiKeyModifier right) {
    return static_cast<UiKeyModifier>(static_cast<uint8_t>(left) |
                                      static_cast<uint8_t>(right));
}

constexpr bool has_ui_key_modifier(UiKeyModifier value, UiKeyModifier flag) {
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
}

enum class UiInputEventType : uint8_t {
    PointerMove,
    PointerDown,
    PointerUp,
    PointerScroll,
    KeyDown,
    TextCommit,
    TextComposition,
    FocusGained,
    FocusLost,
};

enum class UiEventPhase : uint8_t {
    Capture,
    Target,
    Bubble,
};

enum class UiEventReply : uint8_t {
    Ignored,
    Handled,
    StopPropagation,
};

struct UiInputEvent {
    UiInputEventType type = UiInputEventType::PointerMove;
    UiEventPhase phase = UiEventPhase::Target;
    Vec2 pointer_position{};
    Vec2 scroll_delta{};
    UiPointerButton pointer_button = UiPointerButton::None;
    UiKey key = UiKey::Unknown;
    UiKeyModifier modifiers = UiKeyModifier::None;
    std::string text;
    int32_t composition_start = -1;
    int32_t composition_length = -1;
    // True only when a pointer-up lands on the same captured control.
    bool activation = false;
};

struct UiTextComposition {
    std::string text;
    int32_t start = -1;
    int32_t length = -1;
};

// One host frame of platform-neutral input. UI runtime state derives release
// edges from held state, so hosts only need to provide current held/pressed
// button values, mapped key edges, committed UTF-8, and IME composition.
struct UiInputState {
    bool pointer_enabled = true;
    Vec2 pointer_position{};
    std::array<bool, 3> pointer_held{};
    std::array<bool, 3> pointer_pressed{};
    std::array<bool, 3> pointer_released{};
    Vec2 scroll_delta{};
    UiKeyModifier modifiers = UiKeyModifier::None;
    std::vector<UiKey> pressed_keys;
    std::vector<std::string> text_commits;
    std::vector<UiTextComposition> text_compositions;
};

// Clipboard access is an injected host service. Widgets only exchange UTF-8
// text through this interface; SDL, Win32, browser, and test implementations
// stay outside the retained UI object model.
class IUiClipboard {
public:
    virtual ~IUiClipboard() = default;

    [[nodiscard]] virtual snt::core::Expected<std::string> read_text() = 0;
    [[nodiscard]] virtual snt::core::Expected<void> write_text(std::string_view text) = 0;
};

// Useful for headless hosts, tests, and integrations that deliberately do
// not expose the operating-system clipboard.
class UiMemoryClipboard final : public IUiClipboard {
public:
    [[nodiscard]] snt::core::Expected<std::string> read_text() override;
    [[nodiscard]] snt::core::Expected<void> write_text(std::string_view text) override;

    const std::string& text() const noexcept { return text_; }

private:
    std::string text_;
};

struct UiTextInputArea {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t cursor = 0;
};

// UiTextInputService owns IME activation policy while the host owns the native API.
// Candidate coordinates are window units, derived from UiViewport at this
// boundary instead of leaking logical UI coordinates into platform code.
class IUiTextInputPlatform {
public:
    virtual ~IUiTextInputPlatform() = default;

    [[nodiscard]] virtual snt::core::Expected<void> set_text_input_active(bool active) = 0;
    [[nodiscard]] virtual snt::core::Expected<void> set_text_input_area(
        UiTextInputArea area) = 0;
};

enum class UiInteractionState : uint8_t {
    None = 0,
    Hovered = 1u << 0u,
    Pressed = 1u << 1u,
    Focused = 1u << 2u,
    Disabled = 1u << 3u,
};

constexpr UiInteractionState operator|(UiInteractionState left,
                                        UiInteractionState right) {
    return static_cast<UiInteractionState>(static_cast<uint8_t>(left) |
                                           static_cast<uint8_t>(right));
}

constexpr bool has_interaction_state(UiInteractionState value,
                                     UiInteractionState flag) {
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
}

// Theme values are data, not widget-specific globals. A future asset-backed
// theme loader can replace the active value without changing the view API.
struct UiTheme {
    Color button_normal{37, 50, 65, 230};
    Color button_hovered{56, 82, 108, 238};
    Color button_pressed{76, 116, 154, 245};
    Color button_focused{48, 69, 92, 238};
    Color button_disabled{44, 47, 53, 180};
    float button_radius = 4.0f;
};

enum class ViewKind : uint8_t {
    View,
    ViewGroup,
    FlexLayout,
    GridLayout,
    FrameLayout,
    TextView,
    Button,
    ImageView,
    NineSliceView,
    TextInput,
    TextEditor,
    Checkbox,
    Slider,
    VirtualList,
    Modal,
    Tooltip,
    SlotView,
    ScrollView,
    ProgressBar,
    WrappedText,
    PanZoom,
};

}  // namespace snt::ui
