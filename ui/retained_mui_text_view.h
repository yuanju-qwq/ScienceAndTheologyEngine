// Retained-MUI display-only text views.

#pragma once

#include "retained_mui_view.h"

#include <string>
#include <utility>

namespace snt::ui {

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

    void set_bound_text_key(std::string key) { bound_text_key_ = std::move(key); }
    const std::string& bound_text_key() const { return bound_text_key_; }

    // Text bindings belong to text-capable widgets. Editable controls can
    // still use View::bind_value when they need silent model-to-editor sync.
    void bind_text(ViewModel& model, std::string key);

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;

protected:
    std::string text_;
    TextStyle style_{};
    mutable TextLayout cached_layout_{};
    mutable bool dirty_layout_ = true;

private:
    std::string bound_text_key_;
};

class TooltipView final : public TextView {
public:
    explicit TooltipView(std::string id = {});
    ViewKind kind() const override { return ViewKind::Tooltip; }
};

}  // namespace snt::ui
