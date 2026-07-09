// Retained MUI — self-built retained UI system for gameplay screens.
//
// This is the P6 UI path. Gameplay UI is retained-mode, MVVM-driven, and
// rendered through Arc2D draw commands.

#pragma once

#include "ui_draw_data.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
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

enum class Visibility : uint8_t {
    Visible,
    Hidden,
    Gone,
};

enum class ViewKind : uint8_t {
    View,
    ViewGroup,
    LinearLayout,
    FrameLayout,
    TextView,
    Button,
    ImageView,
    SlotView,
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

struct TextLayout {
    std::vector<TextCluster> clusters;
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
};

class TextEngine {
public:
    virtual ~TextEngine() = default;
    virtual const TextEngineCapabilities& capabilities() const = 0;
    virtual TextLayout shape(std::string_view text, const TextStyle& style) = 0;
};

class FallbackTextEngine final : public TextEngine {
public:
    explicit FallbackTextEngine(TextEngineConfig config = {});

    const TextEngineCapabilities& capabilities() const override { return caps_; }
    TextLayout shape(std::string_view text, const TextStyle& style) override;

private:
    void warn_missing_backends_once();

    TextEngineConfig config_{};
    TextEngineCapabilities caps_{};
    bool warned_missing_backends_ = false;
};

using BindingValue = std::variant<std::monostate, bool, int64_t, double, std::string>;

class ViewModel {
public:
    using Observer = std::function<void(std::string_view, const BindingValue&)>;
    using Command = std::function<void(const BindingValue&)>;

    void set(std::string key, BindingValue value);
    const BindingValue* get(std::string_view key) const;

    void bind(std::string key, Observer observer);
    void set_command(std::string name, Command command);
    bool invoke(std::string_view name, BindingValue payload = {});

private:
    std::unordered_map<std::string, BindingValue> values_;
    std::unordered_map<std::string, std::vector<Observer>> observers_;
    std::unordered_map<std::string, Command> commands_;
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

using ArcDrawCommand = std::variant<DrawRectCommand, DrawTextCommand, DrawImageCommand>;

class Arc2DCommandBuffer {
public:
    void clear();
    void rect(Rect rect, Color color, float radius = 0.0f);
    void text(Rect rect, std::string text, TextStyle style, TextLayout layout);
    void image(Rect rect, std::string image_key, Color tint = {});

    const std::vector<ArcDrawCommand>& commands() const { return commands_; }

private:
    std::vector<ArcDrawCommand> commands_;
};

class Arc2DRenderer {
public:
    // Converts supported Arc2D primitives into renderer draw data. Text
    // commands stay in the command buffer until the HarfBuzz/ICU/SDF text
    // backend is wired to glyph atlas pages.
    UiDrawData build_draw_data(const Arc2DCommandBuffer& commands) const;

private:
    static void append_rect(UiDrawData& out, Rect rect, Color color);
};

class View {
public:
    explicit View(std::string id = {});
    virtual ~View() = default;

    View(const View&) = delete;
    View& operator=(const View&) = delete;

    virtual ViewKind kind() const { return ViewKind::View; }

    void set_id(std::string id) { id_ = std::move(id); }
    const std::string& id() const { return id_; }

    void set_layout_params(LayoutParams params) { layout_params_ = params; }
    const LayoutParams& layout_params() const { return layout_params_; }
    LayoutParams& layout_params() { return layout_params_; }

    void set_background(Color color, float radius = 0.0f);
    void set_visibility(Visibility visibility) { visibility_ = visibility; }
    Visibility visibility() const { return visibility_; }

    Vec2 measured_size() const { return measured_size_; }
    Rect bounds() const { return bounds_; }

    void set_bound_text_key(std::string key) { bound_text_key_ = std::move(key); }
    const std::string& bound_text_key() const { return bound_text_key_; }

    void bind_text(ViewModel& model, std::string key);

    virtual void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine);
    virtual void layout(Rect bounds);
    virtual void paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const;

protected:
    static float resolve_axis(float requested, MeasureSpec spec, float desired);
    static float clamp_axis(float value, MeasureSpec spec);

    std::string id_;
    LayoutParams layout_params_{};
    Visibility visibility_ = Visibility::Visible;
    Vec2 measured_size_{};
    Rect bounds_{};
    std::optional<DrawRectCommand> background_;
    std::string bound_text_key_;
};

class TextView : public View {
public:
    explicit TextView(std::string id = {});
    ViewKind kind() const override { return ViewKind::TextView; }

    void set_text(std::string text) { text_ = std::move(text); dirty_layout_ = true; }
    const std::string& text() const { return text_; }

    void set_text_style(TextStyle style) { style_ = style; dirty_layout_ = true; }
    const TextStyle& text_style() const { return style_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const override;

protected:
    std::string text_;
    TextStyle style_{};
    mutable TextLayout cached_layout_{};
    mutable bool dirty_layout_ = true;
};

class Button : public TextView {
public:
    explicit Button(std::string id = {});
    ViewKind kind() const override { return ViewKind::Button; }

    void set_command(std::string command) { command_ = std::move(command); }
    const std::string& command() const { return command_; }
    bool click(ViewModel& model, BindingValue payload = {}) const;

private:
    std::string command_;
};

class ImageView : public View {
public:
    explicit ImageView(std::string id = {});
    ViewKind kind() const override { return ViewKind::ImageView; }

    void set_image_key(std::string image_key) { image_key_ = std::move(image_key); }
    const std::string& image_key() const { return image_key_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const override;

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

    void set_slot_state(SlotState state) { state_ = std::move(state); }
    const SlotState& slot_state() const { return state_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const override;

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
    View* find(std::string_view id);
    const View* find(std::string_view id) const;

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;
    void paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const override;

protected:
    std::vector<std::unique_ptr<View>> children_;
};

class LinearLayout : public ViewGroup {
public:
    explicit LinearLayout(std::string id = {});
    ViewKind kind() const override { return ViewKind::LinearLayout; }

    void set_orientation(Orientation orientation) { orientation_ = orientation; }
    Orientation orientation() const { return orientation_; }
    void set_spacing(float spacing) { spacing_ = spacing; }
    float spacing() const { return spacing_; }
    void set_padding(Insets padding) { padding_ = padding; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;

private:
    Orientation orientation_ = Orientation::Vertical;
    float spacing_ = 0.0f;
    Insets padding_{};
};

class FrameLayout : public ViewGroup {
public:
    explicit FrameLayout(std::string id = {});
    ViewKind kind() const override { return ViewKind::FrameLayout; }

    void set_padding(Insets padding) { padding_ = padding; }
    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void layout(Rect bounds) override;

private:
    Insets padding_{};
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

struct UiFrameResult {
    Arc2DCommandBuffer commands;
    UiDrawData draw_data;
};

class UiRuntime {
public:
    UiRuntime();

    TextEngine& text_engine() { return text_engine_; }
    const TextEngine& text_engine() const { return text_engine_; }

    UiFrameResult build_frame(View& root, Vec2 viewport);

private:
    FallbackTextEngine text_engine_;
    Arc2DRenderer renderer_;
};

}  // namespace snt::ui
