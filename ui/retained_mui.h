// Retained MUI — self-built retained UI system for gameplay screens.
//
// This is the P6 UI path. Gameplay UI is retained-mode, MVVM-driven, and
// rendered through Arc2D draw commands.

#pragma once

#include "ui_draw_data.h"
#include "ui_image_registry.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace snt::core { class RuntimePathResolver; }

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
    Left,
    Right,
    Up,
    Down,
};

enum class UiInputEventType : uint8_t {
    PointerMove,
    PointerDown,
    PointerUp,
    PointerScroll,
    KeyDown,
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
    // True only when a pointer-up lands on the same captured control.
    bool activation = false;
};

// One host frame of platform-neutral input. UI runtime state derives release
// edges from held state, so hosts only need to provide current held/pressed
// button values and mapped key edges.
struct UiInputState {
    bool pointer_enabled = true;
    Vec2 pointer_position{};
    std::array<bool, 3> pointer_held{};
    std::array<bool, 3> pointer_pressed{};
    std::array<bool, 3> pointer_released{};
    Vec2 scroll_delta{};
    std::vector<UiKey> pressed_keys;
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
    LinearLayout,
    GridLayout,
    FrameLayout,
    TextView,
    Button,
    ImageView,
    SlotView,
    ScrollView,
};

struct TextStyle {
    float size_px = 16.0f;
    Color color{230, 236, 245, 255};
    bool sdf = true;
    bool emoji = true;
};

enum class TextDirection : uint8_t {
    LeftToRight,
    RightToLeft,
};

struct TextCluster {
    std::string utf8;
    uint32_t first_codepoint = 0;
    float advance = 0.0f;
    bool is_emoji = false;
    bool is_cjk = false;
    bool requires_color = false;
};

// A raster-ready HarfBuzz glyph. Metrics are derived from the same
// FreeType face used during shaping; UVs address the dynamic Unicode atlas.
struct TextGlyph {
    uint32_t glyph_id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    bool drawable = false;
    bool color = false;
};

struct TextLayout {
    std::vector<TextCluster> clusters;
    std::vector<TextGlyph> glyphs;
    std::shared_ptr<const UiGlyphAtlas> glyph_atlas;
    Vec2 size{};
    bool contains_emoji = false;
    bool contains_cjk = false;
    TextDirection direction = TextDirection::LeftToRight;
};

struct TextEngineCapabilities {
    bool harfbuzz = false;
    bool icu = false;
    bool bidi = false;
    bool sdf = false;
    bool color_emoji = false;
};

struct TextEngineConfig {
    bool require_harfbuzz = true;
    bool require_icu = true;
    bool require_sdf = true;
    bool require_color_emoji = true;
    // Ordered font family for real glyph coverage, not a compatibility path.
    // All entries participate in HarfBuzz/FreeType shaping and rasterization.
    std::vector<std::string> font_paths{
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/seguiemj.ttf",
    };
    std::string locale = "zh-Hans";
    std::string icu_data_path = "third_party/icu4c/icudt_godot.dat";
};

class TextEngine {
public:
    virtual ~TextEngine() = default;
    virtual const TextEngineCapabilities& capabilities() const = 0;
    virtual TextLayout shape(std::string_view text, const TextStyle& style) = 0;
};

// Production MUI text engine. It has no simplified shaping path: UTF-8 is
// converted to ICU UTF-16, BiDi/grapheme analysis runs through ICU, and each
// font run is shaped by HarfBuzz using a FreeType face. Missing required
// backends leave the engine unavailable rather than silently degrading text.
class UnicodeTextEngine final : public TextEngine {
public:
    // `paths` is borrowed only during construction to load engine-owned ICU
    // data; font paths remain explicit TextEngineConfig entries.
    UnicodeTextEngine(const snt::core::RuntimePathResolver& paths,
                      TextEngineConfig config = {});
    ~UnicodeTextEngine() override;

    UnicodeTextEngine(const UnicodeTextEngine&) = delete;
    UnicodeTextEngine& operator=(const UnicodeTextEngine&) = delete;

    const TextEngineCapabilities& capabilities() const override { return caps_; }
    TextLayout shape(std::string_view text, const TextStyle& style) override;
    bool available() const { return available_; }
    const std::string& initialization_error() const { return initialization_error_; }

private:
    struct Impl;

    TextEngineConfig config_{};
    TextEngineCapabilities caps_{};
    std::unique_ptr<Impl> impl_;
    bool available_ = false;
    bool logged_unavailable_ = false;
    std::string initialization_error_;
};

using BindingValue = std::variant<std::monostate, bool, int64_t, double, std::string>;

class ViewModel {
private:
    struct State;

public:
    using Observer = std::function<void(std::string_view, const BindingValue&)>;

    // A subscription owns one observer registration. It is movable but not
    // copyable; destroying it is safe even after its ViewModel has died.
    class Subscription {
    public:
        Subscription() = default;
        ~Subscription();

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;

        void reset();
        bool connected() const;

    private:
        friend class ViewModel;

        Subscription(std::weak_ptr<State> state, std::string key, uint64_t observer_id);

        std::weak_ptr<State> state_;
        std::string key_;
        uint64_t observer_id_ = 0;
    };

    ViewModel();
    ~ViewModel();

    ViewModel(const ViewModel&) = delete;
    ViewModel& operator=(const ViewModel&) = delete;
    ViewModel(ViewModel&&);
    ViewModel& operator=(ViewModel&&);

    void set(std::string key, BindingValue value);
    const BindingValue* get(std::string_view key) const;

    [[nodiscard]] Subscription bind(std::string key, Observer observer);

private:
    std::shared_ptr<State> state_;
};

struct DrawTextCommand {
    Rect rect{};
    std::string text;
    TextStyle style{};
    TextLayout layout{};
};

struct DrawRectCommand {
    Rect rect{};
    Color color{};
    float radius = 0.0f;
};

struct DrawImageCommand {
    Rect rect{};
    std::string image_key;
    Color tint{255, 255, 255, 255};
};

struct PushClipCommand {
    Rect rect{};
};

struct PopClipCommand {};

using ArcDrawCommand = std::variant<DrawRectCommand, DrawTextCommand, DrawImageCommand,
                                    PushClipCommand, PopClipCommand>;

class Arc2DCommandBuffer {
public:
    void clear();
    void rect(Rect rect, Color color, float radius = 0.0f);
    void text(Rect rect, std::string text, TextStyle style, TextLayout layout);
    void image(Rect rect, std::string image_key, Color tint = {});
    void push_clip(Rect rect);
    void pop_clip();

    const std::vector<ArcDrawCommand>& commands() const { return commands_; }

private:
    std::vector<ArcDrawCommand> commands_;
};

class Arc2DRenderer {
public:
    // Converts Arc2D primitives into renderer batches. Text is lowered from
    // HarfBuzz/FreeType glyphs, images resolve through the explicit registry,
    // and nested clips become dynamic Vulkan scissor state.
    explicit Arc2DRenderer(UiImageRegistry& images) : images_(&images) {}

    UiDrawData build_draw_data(const Arc2DCommandBuffer& commands) const;

private:
    static void append_rect(UiDrawData& out, Rect rect, Color color, float radius,
                            UiClipRect clip);
    static void append_glyph_text(UiDrawData& out, const DrawTextCommand& text,
                                  UiClipRect clip);
    void append_image(UiDrawData& out, const DrawImageCommand& image,
                      UiClipRect clip) const;

    UiImageRegistry* images_ = nullptr;
};

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

class SlotView : public View {
public:
    struct SlotState {
        std::string item_key;
        int32_t count = 0;
        bool selected = false;
    };

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

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;

private:
    SlotState state_{};
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

class LinearLayout : public ViewGroup {
public:
    explicit LinearLayout(std::string id = {});
    ViewKind kind() const override { return ViewKind::LinearLayout; }

    void set_orientation(Orientation orientation) {
        if (orientation_ == orientation) return;
        orientation_ = orientation;
        mark_layout_dirty();
    }
    Orientation orientation() const { return orientation_; }
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
using UiActionDispatcher = std::function<void(std::string_view action_id)>;

struct UiScreenMountContext {
    Vec2 viewport{};
    UiImageRegistry& images;
    UiActionDispatcher dispatch_action;
};

struct UiScreenFrameContext {
    Vec2 viewport{};
    UiImageRegistry& images;
};

using UiScreenUpdater = std::function<void(View&, const UiScreenFrameContext&)>;

struct UiScreenMount {
    std::unique_ptr<View> root;
    // Called once per host frame after mounting. Implementations must be
    // cheap when their ViewModel revision has not changed.
    UiScreenUpdater update;
};

using UiScreenFactory = std::function<snt::core::Expected<UiScreenMount>(
    const UiScreenMountContext&)>;

struct UiScreenRegistration {
    // `owner_id` is the game or mod namespace, for example "builtin" or
    // "example_expansion". `screen_id` is unique within that owner.
    std::string owner_id;
    std::string screen_id;
    UiLayer layer = UiLayer::Screen;
    bool initially_visible = false;
    UiScreenFactory factory;
    // Resource action IDs stay declarative until the host registers this
    // screen. The host may route them to a command bus, a script callback, or
    // a network request without exposing native function pointers in assets.
    UiActionDispatcher dispatch_action;
};

struct UiScreenSubmission {
    UiLayer layer = UiLayer::Screen;
    UiLayerInputPolicy input_policy{};
    // Borrowed from UiLayerStack. It remains valid until the stack mutates or
    // the stack is destroyed.
    View* root = nullptr;
};

class UiLayerStack final {
public:
    [[nodiscard]] snt::core::Expected<void> register_screen(UiScreenRegistration registration);
    // Replaces one owner's complete screen set after all registrations have
    // been validated. Resource loaders use this for transaction-like Mod
    // reloads, so a malformed replacement never partially removes live UI.
    [[nodiscard]] snt::core::Expected<void> replace_owner_screens(
        std::string_view owner_id,
        std::vector<UiScreenRegistration> registrations);
    [[nodiscard]] snt::core::Expected<void> set_visible(std::string_view owner_id,
                                                          std::string_view screen_id,
                                                          bool visible);
    [[nodiscard]] snt::core::Expected<void> unregister_screen(std::string_view owner_id,
                                                               std::string_view screen_id);

    // Removes every screen owned by one content package. This is the unload
    // hook needed by hot-reloaded mods before their captured state is freed.
    [[nodiscard]] size_t unregister_owner(std::string_view owner_id);
    [[nodiscard]] bool is_registered(std::string_view owner_id,
                                     std::string_view screen_id) const;
    [[nodiscard]] bool is_visible(std::string_view owner_id,
                                  std::string_view screen_id) const;
    [[nodiscard]] bool is_mounted(std::string_view owner_id,
                                  std::string_view screen_id) const;
    [[nodiscard]] size_t screen_count() const { return screens_.size(); }

    // Mounts visible screens that do not yet own a root and updates existing
    // mounts. The returned storage is reused by the stack to avoid per-frame
    // allocations; callers consume it before invoking another stack method.
    [[nodiscard]] const std::vector<UiScreenSubmission>& prepare_frame(
        const UiScreenFrameContext& context);

    // Screen removal/hide invalidates focus and pointer capture rooted in the
    // old tree. The host drains this after mutations and before input routing.
    [[nodiscard]] std::vector<std::string> take_invalidated_root_ids();

private:
    struct ScreenRecord {
        UiScreenRegistration registration;
        bool visible = false;
        bool mount_failure_logged = false;
        UiScreenMount mounted;
    };

    ScreenRecord* find_record(std::string_view owner_id, std::string_view screen_id);
    const ScreenRecord* find_record(std::string_view owner_id,
                                    std::string_view screen_id) const;
    void invalidate_interaction(const ScreenRecord& record);
    static bool valid_key_part(std::string_view value);
    static std::string qualified_id(std::string_view owner_id, std::string_view screen_id);

    std::vector<ScreenRecord> screens_;
    std::vector<UiScreenSubmission> frame_submissions_;
    std::vector<std::string> invalidated_root_ids_;
};

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

    // Hosts first layout all submitted roots, route one input frame from the
    // highest interactive layer downward, then synchronize states and paint.
    // Root and child ids must be unique within their UI layer so transient
    // view rebuilding can safely retain focus, hover, and pointer capture.
    void layout(View& root, Vec2 viewport);
    void begin_input_frame(UiInputState input);
    // Each dispatcher returns true when this root owns that input channel and
    // lower layers must not receive that channel for the current host frame.
    bool dispatch_pointer_input(View& root);
    bool dispatch_keyboard_input(View& root);
    // Clears a capture whose pointer-up could not be routed because a newly
    // visible blocking layer superseded its former root. The host invokes this
    // once after all layer policies have been evaluated for the frame.
    void end_input_frame();
    void synchronize_interaction_state(View& root);
    UiFrameResult paint(View& root);
    UiDrawData build_draw_data(const Arc2DCommandBuffer& commands);
    void set_focus_scope(std::string root_id);
    void cancel_interaction_for_root(std::string_view root_id);
    void clear_interaction_state();

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

    UnicodeTextEngine text_engine_;
    UiImageRegistry images_;
    UiLayerStack layers_;
    Arc2DRenderer renderer_;
    UiTheme theme_{};
    UiInputState input_{};
    std::array<bool, 3> previous_pointer_held_{};
    std::array<bool, 3> pointer_released_{};
    ElementId hovered_;
    ElementId focused_;
    ElementId pointer_capture_;
    std::string focus_scope_root_;
    // Main-thread routing scratch storage. Retaining these avoids allocating a
    // hit/event path for every submitted root on every input frame.
    std::vector<View*> hit_path_scratch_;
    std::vector<View*> event_path_scratch_;
};

}  // namespace snt::ui
