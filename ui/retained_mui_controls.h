// Retained-MUI standard interactive and image controls.

#pragma once

#include "retained_mui_text_view.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace snt::ui {

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

}  // namespace snt::ui
