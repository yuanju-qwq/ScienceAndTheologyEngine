#define SNT_LOG_CHANNEL "ui"
#include "retained_mui.h"

#include "core/log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace snt::ui {

namespace {

bool is_rtl_codepoint(uint32_t cp) {
    return (cp >= 0x0590u && cp <= 0x08FFu) ||
           (cp >= 0xFB1Du && cp <= 0xFDFFu) ||
           (cp >= 0xFE70u && cp <= 0xFEFFu);
}

bool is_cjk_codepoint(uint32_t cp) {
    return (cp >= 0x3400u && cp <= 0x4DBFu) ||
           (cp >= 0x4E00u && cp <= 0x9FFFu) ||
           (cp >= 0xF900u && cp <= 0xFAFFu);
}

bool is_emoji_codepoint(uint32_t cp) {
    return (cp >= 0x1F000u && cp <= 0x1FAFFu) ||
           (cp >= 0x2600u && cp <= 0x27BFu);
}

uint32_t decode_utf8(std::string_view s, size_t& offset) {
    const auto read = [&](size_t i) -> uint8_t {
        return static_cast<uint8_t>(s[offset + i]);
    };

    if (offset >= s.size()) return 0;

    const uint8_t b0 = read(0);
    if (b0 < 0x80u) {
        ++offset;
        return b0;
    }
    if ((b0 & 0xE0u) == 0xC0u && offset + 1 < s.size()) {
        const uint32_t cp = ((b0 & 0x1Fu) << 6) | (read(1) & 0x3Fu);
        offset += 2;
        return cp;
    }
    if ((b0 & 0xF0u) == 0xE0u && offset + 2 < s.size()) {
        const uint32_t cp = ((b0 & 0x0Fu) << 12) |
                            ((read(1) & 0x3Fu) << 6) |
                            (read(2) & 0x3Fu);
        offset += 3;
        return cp;
    }
    if ((b0 & 0xF8u) == 0xF0u && offset + 3 < s.size()) {
        const uint32_t cp = ((b0 & 0x07u) << 18) |
                            ((read(1) & 0x3Fu) << 12) |
                            ((read(2) & 0x3Fu) << 6) |
                            (read(3) & 0x3Fu);
        offset += 4;
        return cp;
    }

    ++offset;
    return 0xFFFDu;
}

float text_cluster_advance(uint32_t cp, const TextStyle& style) {
    if (cp == '\n') return 0.0f;
    if (is_emoji_codepoint(cp)) return style.size_px * 1.1f;
    if (is_cjk_codepoint(cp)) return style.size_px;
    if (cp < 0x80u) return style.size_px * 0.58f;
    return style.size_px * 0.72f;
}

template <typename T>
T* find_view_recursive(std::vector<std::unique_ptr<View>>& children, std::string_view id) {
    for (auto& child : children) {
        if (child->id() == id) return child.get();
        if (auto* group = dynamic_cast<ViewGroup*>(child.get())) {
            if (auto* found = group->find(id)) return found;
        }
    }
    return nullptr;
}

template <typename T>
const T* find_view_recursive(const std::vector<std::unique_ptr<View>>& children, std::string_view id) {
    for (const auto& child : children) {
        if (child->id() == id) return child.get();
        if (const auto* group = dynamic_cast<const ViewGroup*>(child.get())) {
            if (auto* found = group->find(id)) return found;
        }
    }
    return nullptr;
}

std::string binding_value_to_string(const BindingValue& value) {
    if (const auto* s = std::get_if<std::string>(&value)) return *s;
    if (const auto* b = std::get_if<bool>(&value)) return *b ? "true" : "false";
    if (const auto* i = std::get_if<int64_t>(&value)) return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&value)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f", *d);
        return buf;
    }
    return {};
}

std::array<uint8_t, 7> bitmap_glyph(char ch) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    switch (c) {
        case '0': return {0x0Eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0Eu};
        case '1': return {0x04u, 0x0Cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu};
        case '2': return {0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1Fu};
        case '3': return {0x1Eu, 0x01u, 0x01u, 0x0Eu, 0x01u, 0x01u, 0x1Eu};
        case '4': return {0x02u, 0x06u, 0x0Au, 0x12u, 0x1Fu, 0x02u, 0x02u};
        case '5': return {0x1Fu, 0x10u, 0x1Eu, 0x01u, 0x01u, 0x11u, 0x0Eu};
        case '6': return {0x06u, 0x08u, 0x10u, 0x1Eu, 0x11u, 0x11u, 0x0Eu};
        case '7': return {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u};
        case '8': return {0x0Eu, 0x11u, 0x11u, 0x0Eu, 0x11u, 0x11u, 0x0Eu};
        case '9': return {0x0Eu, 0x11u, 0x11u, 0x0Fu, 0x01u, 0x02u, 0x0Cu};
        case 'A': return {0x0Eu, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u};
        case 'B': return {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x11u, 0x11u, 0x1Eu};
        case 'C': return {0x0Eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0Eu};
        case 'D': return {0x1Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1Eu};
        case 'E': return {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x1Fu};
        case 'F': return {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x10u};
        case 'G': return {0x0Eu, 0x11u, 0x10u, 0x17u, 0x11u, 0x11u, 0x0Eu};
        case 'H': return {0x11u, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u};
        case 'I': return {0x0Eu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu};
        case 'J': return {0x07u, 0x02u, 0x02u, 0x02u, 0x12u, 0x12u, 0x0Cu};
        case 'K': return {0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u};
        case 'L': return {0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1Fu};
        case 'M': return {0x11u, 0x1Bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u};
        case 'N': return {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u};
        case 'O': return {0x0Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu};
        case 'P': return {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x10u, 0x10u, 0x10u};
        case 'Q': return {0x0Eu, 0x11u, 0x11u, 0x11u, 0x15u, 0x12u, 0x0Du};
        case 'R': return {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x14u, 0x12u, 0x11u};
        case 'S': return {0x0Fu, 0x10u, 0x10u, 0x0Eu, 0x01u, 0x01u, 0x1Eu};
        case 'T': return {0x1Fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u};
        case 'U': return {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu};
        case 'V': return {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Au, 0x04u};
        case 'W': return {0x11u, 0x11u, 0x11u, 0x15u, 0x15u, 0x15u, 0x0Au};
        case 'X': return {0x11u, 0x11u, 0x0Au, 0x04u, 0x0Au, 0x11u, 0x11u};
        case 'Y': return {0x11u, 0x11u, 0x0Au, 0x04u, 0x04u, 0x04u, 0x04u};
        case 'Z': return {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x1Fu};
        case '.': return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x0Cu};
        case ':': return {0x00u, 0x0Cu, 0x0Cu, 0x00u, 0x0Cu, 0x0Cu, 0x00u};
        case '-': return {0x00u, 0x00u, 0x00u, 0x1Fu, 0x00u, 0x00u, 0x00u};
        case '/': return {0x01u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x10u};
        case ' ': return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
        default: return {0x1Fu, 0x11u, 0x01u, 0x02u, 0x04u, 0x00u, 0x04u};
    }
}

MeasureSpec child_spec(float parent_inner, float requested, float margin_a, float margin_b) {
    const float available = std::max(0.0f, parent_inner - margin_a - margin_b);
    if (requested > 0.0f) return {.size = requested, .mode = MeasureMode::Exactly};
    if (requested == 0.0f) return {.size = available, .mode = MeasureMode::Exactly};
    return {.size = available, .mode = MeasureMode::AtMost};
}

}  // namespace

FallbackTextEngine::FallbackTextEngine(TextEngineConfig config)
    : config_(config) {
    caps_.sdf = true;
}

TextLayout FallbackTextEngine::shape(std::string_view text, const TextStyle& style) {
    warn_missing_backends_once();

    TextLayout layout;
    float line_width = 0.0f;
    float max_width = 0.0f;
    int32_t line_count = 1;

    for (size_t offset = 0; offset < text.size();) {
        const size_t start = offset;
        const uint32_t cp = decode_utf8(text, offset);
        TextCluster cluster;
        cluster.utf8 = std::string(text.substr(start, offset - start));
        cluster.first_codepoint = cp;
        cluster.is_emoji = is_emoji_codepoint(cp);
        cluster.is_cjk = is_cjk_codepoint(cp);
        cluster.requires_color = cluster.is_emoji;
        cluster.advance = text_cluster_advance(cp, style);

        layout.contains_emoji = layout.contains_emoji || cluster.is_emoji;
        layout.contains_cjk = layout.contains_cjk || cluster.is_cjk;
        if (is_rtl_codepoint(cp)) {
            layout.direction = TextDirection::RightToLeft;
        }

        if (cp == '\n') {
            max_width = std::max(max_width, line_width);
            line_width = 0.0f;
            ++line_count;
        } else {
            line_width += cluster.advance;
        }
        layout.clusters.push_back(std::move(cluster));
    }

    max_width = std::max(max_width, line_width);
    layout.size = {max_width, std::max(style.size_px * 1.25f,
                                       style.size_px * 1.25f * line_count)};
    return layout;
}

void FallbackTextEngine::warn_missing_backends_once() {
    if (warned_missing_backends_) return;
    warned_missing_backends_ = true;

    static bool warned_process = false;
    if (warned_process) return;

    if ((config_.require_harfbuzz && !caps_.harfbuzz) ||
        (config_.require_icu && !caps_.icu) ||
        (config_.require_color_emoji && !caps_.color_emoji)) {
        SNT_LOG_WARN("P6 TextEngine using fallback shaper; HarfBuzz/ICU/color emoji backends are declared but not linked yet");
        warned_process = true;
    }
}

void ViewModel::set(std::string key, BindingValue value) {
    const std::string stable_key = key;
    values_[std::move(key)] = value;
    auto it = observers_.find(stable_key);
    if (it == observers_.end()) return;
    for (auto& observer : it->second) {
        if (observer) observer(stable_key, values_[stable_key]);
    }
}

const BindingValue* ViewModel::get(std::string_view key) const {
    auto it = values_.find(std::string(key));
    return it == values_.end() ? nullptr : &it->second;
}

void ViewModel::bind(std::string key, Observer observer) {
    if (!observer) return;
    auto& list = observers_[key];
    list.push_back(observer);
    auto value_it = values_.find(key);
    if (value_it != values_.end()) {
        list.back()(key, value_it->second);
    }
}

void ViewModel::set_command(std::string name, Command command) {
    commands_[std::move(name)] = std::move(command);
}

bool ViewModel::invoke(std::string_view name, BindingValue payload) {
    auto it = commands_.find(std::string(name));
    if (it == commands_.end() || !it->second) {
        SNT_LOG_WARN("ViewModel command '%.*s' is not registered",
                     static_cast<int>(name.size()), name.data());
        return false;
    }
    it->second(payload);
    return true;
}

void Arc2DCommandBuffer::clear() {
    commands_.clear();
}

void Arc2DCommandBuffer::rect(Rect rect, Color color, float radius) {
    commands_.push_back(DrawRectCommand{rect, color, radius});
}

void Arc2DCommandBuffer::text(Rect rect, std::string text, TextStyle style, TextLayout layout) {
    commands_.push_back(DrawTextCommand{rect, std::move(text), style, std::move(layout)});
}

void Arc2DCommandBuffer::image(Rect rect, std::string image_key, Color tint) {
    commands_.push_back(DrawImageCommand{rect, std::move(image_key), tint});
}

UiDrawData Arc2DRenderer::build_draw_data(const Arc2DCommandBuffer& commands) const {
    UiDrawData out;
    for (const auto& cmd : commands.commands()) {
        if (const auto* rect = std::get_if<DrawRectCommand>(&cmd)) {
            append_rect(out, rect->rect, rect->color);
        } else if (const auto* image = std::get_if<DrawImageCommand>(&cmd)) {
            append_rect(out, image->rect, image->tint);
        } else if (const auto* text = std::get_if<DrawTextCommand>(&cmd)) {
            append_bitmap_text(out, *text);
        }
    }
    return out;
}

void Arc2DRenderer::append_rect(UiDrawData& out, Rect rect, Color color) {
    if (rect.size.x <= 0.0f || rect.size.y <= 0.0f) return;
    if (out.vertices.size() + 4 > 0xFFFFu) {
        SNT_LOG_WARN("Arc2DRenderer draw buffer overflow; dropping rect");
        return;
    }

    const uint16_t base = static_cast<uint16_t>(out.vertices.size());
    UiVertex v{};
    v.uv[0] = -1.0f;
    v.uv[1] = -1.0f;
    v.color[0] = color.r;
    v.color[1] = color.g;
    v.color[2] = color.b;
    v.color[3] = color.a;

    const float x0 = rect.pos.x;
    const float y0 = rect.pos.y;
    const float x1 = rect.pos.x + rect.size.x;
    const float y1 = rect.pos.y + rect.size.y;

    v.position[0] = x0; v.position[1] = y0; out.vertices.push_back(v);
    v.position[0] = x0; v.position[1] = y1; out.vertices.push_back(v);
    v.position[0] = x1; v.position[1] = y1; out.vertices.push_back(v);
    v.position[0] = x1; v.position[1] = y0; out.vertices.push_back(v);

    out.indices.push_back(base + 0);
    out.indices.push_back(base + 1);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 3);
}

void Arc2DRenderer::append_bitmap_text(UiDrawData& out, const DrawTextCommand& text) {
    const float cell = std::max(1.0f, text.style.size_px / 7.0f);
    const float advance = cell * 6.0f;
    float pen_x = text.rect.pos.x;
    float pen_y = text.rect.pos.y;

    for (unsigned char raw : text.text) {
        if (raw == '\n') {
            pen_x = text.rect.pos.x;
            pen_y += cell * 8.0f;
            continue;
        }
        if (raw >= 0x80u) {
            continue;
        }

        const auto rows = bitmap_glyph(static_cast<char>(raw));
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (1u << (4 - col))) == 0u) continue;
                append_rect(out,
                            {.pos = {pen_x + static_cast<float>(col) * cell,
                                     pen_y + static_cast<float>(row) * cell},
                             .size = {cell, cell}},
                            text.style.color);
            }
        }
        pen_x += advance;
    }
}

View::View(std::string id)
    : id_(std::move(id)) {}

void View::set_background(Color color, float radius) {
    background_ = DrawRectCommand{{}, color, radius};
}

void View::bind_text(ViewModel& model, std::string key) {
    set_bound_text_key(key);
    model.bind(key, [this](std::string_view, const BindingValue& value) {
        if (auto* text = dynamic_cast<TextView*>(this)) {
            text->set_text(binding_value_to_string(value));
        }
    });
}

void View::measure(MeasureSpec width, MeasureSpec height, TextEngine&) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, 0.0f);
    measured_size_.y = resolve_axis(layout_params_.height, height, 0.0f);
}

void View::layout(Rect bounds) {
    bounds_ = bounds;
}

void View::paint(Arc2DCommandBuffer& out, TextEngine&) const {
    if (visibility_ != Visibility::Visible) return;
    if (background_) {
        out.rect(bounds_, background_->color, background_->radius);
    }
}

float View::resolve_axis(float requested, MeasureSpec spec, float desired) {
    if (requested > 0.0f) return requested;
    if (requested == 0.0f && spec.mode != MeasureMode::Unspecified) return spec.size;
    return clamp_axis(desired, spec);
}

float View::clamp_axis(float value, MeasureSpec spec) {
    switch (spec.mode) {
        case MeasureMode::Exactly: return spec.size;
        case MeasureMode::AtMost: return std::min(value, spec.size);
        case MeasureMode::Unspecified: return value;
    }
    return value;
}

TextView::TextView(std::string id)
    : View(std::move(id)) {}

void TextView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    cached_layout_ = text_engine.shape(text_, style_);
    dirty_layout_ = false;
    const float desired_w = cached_layout_.size.x + layout_params_.margin.left + layout_params_.margin.right;
    const float desired_h = cached_layout_.size.y + layout_params_.margin.top + layout_params_.margin.bottom;
    measured_size_.x = resolve_axis(layout_params_.width, width, desired_w);
    measured_size_.y = resolve_axis(layout_params_.height, height, desired_h);
}

void TextView::paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const {
    View::paint(out, text_engine);
    if (visibility_ != Visibility::Visible || text_.empty()) return;
    if (dirty_layout_) {
        cached_layout_ = text_engine.shape(text_, style_);
        dirty_layout_ = false;
    }
    out.text(bounds_, text_, style_, cached_layout_);
}

Button::Button(std::string id)
    : TextView(std::move(id)) {
    set_background({37, 50, 65, 230}, 4.0f);
    TextStyle style = text_style();
    style.color = {240, 245, 255, 255};
    set_text_style(style);
}

bool Button::click(ViewModel& model, BindingValue payload) const {
    if (command_.empty()) return false;
    return model.invoke(command_, std::move(payload));
}

ImageView::ImageView(std::string id)
    : View(std::move(id)) {}

void ImageView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    (void)text_engine;
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, 32.0f);
    measured_size_.y = resolve_axis(layout_params_.height, height, 32.0f);
}

void ImageView::paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const {
    View::paint(out, text_engine);
    if (visibility_ != Visibility::Visible || image_key_.empty()) return;
    out.image(bounds_, image_key_, tint_);
}

SlotView::SlotView(std::string id)
    : View(std::move(id)) {}

void SlotView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    (void)text_engine;
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, 36.0f);
    measured_size_.y = resolve_axis(layout_params_.height, height, 36.0f);
}

void SlotView::paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const {
    if (visibility_ != Visibility::Visible) return;
    const Color base = state_.selected ? Color{84, 128, 176, 255}
                                       : Color{34, 38, 48, 255};
    out.rect(bounds_, base, 3.0f);
    if (!state_.item_key.empty() && state_.count > 0) {
        const Rect icon{
            .pos = {bounds_.pos.x + 4.0f, bounds_.pos.y + 4.0f},
            .size = {std::max(0.0f, bounds_.size.x - 8.0f),
                     std::max(0.0f, bounds_.size.y - 8.0f)},
        };
        out.image(icon, state_.item_key, {255, 255, 255, 255});
        if (state_.count > 1) {
            TextStyle style;
            style.size_px = 11.0f;
            style.color = {255, 255, 255, 255};
            auto text = std::to_string(state_.count);
            auto layout = text_engine.shape(text, style);
            out.text(bounds_, std::move(text), style, std::move(layout));
        }
    }
}

ViewGroup::ViewGroup(std::string id)
    : View(std::move(id)) {}

View& ViewGroup::add_child(std::unique_ptr<View> child) {
    children_.push_back(std::move(child));
    return *children_.back();
}

View* ViewGroup::find(std::string_view id) {
    if (id_ == id) return this;
    return find_view_recursive<View>(children_, id);
}

const View* ViewGroup::find(std::string_view id) const {
    if (id_ == id) return this;
    return find_view_recursive<View>(children_, id);
}

void ViewGroup::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }

    float max_w = 0.0f;
    float max_h = 0.0f;
    for (auto& child : children_) {
        const auto& lp = child->layout_params();
        child->measure(child_spec(width.size, lp.width, lp.margin.left, lp.margin.right),
                       child_spec(height.size, lp.height, lp.margin.top, lp.margin.bottom),
                       text_engine);
        max_w = std::max(max_w, child->measured_size().x + lp.margin.left + lp.margin.right);
        max_h = std::max(max_h, child->measured_size().y + lp.margin.top + lp.margin.bottom);
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, max_w);
    measured_size_.y = resolve_axis(layout_params_.height, height, max_h);
}

void ViewGroup::layout(Rect bounds) {
    View::layout(bounds);
    for (auto& child : children_) {
        const auto& lp = child->layout_params();
        const Rect child_bounds{
            .pos = {bounds.pos.x + lp.margin.left, bounds.pos.y + lp.margin.top},
            .size = {child->measured_size().x, child->measured_size().y},
        };
        child->layout(child_bounds);
    }
}

void ViewGroup::paint(Arc2DCommandBuffer& out, TextEngine& text_engine) const {
    View::paint(out, text_engine);
    if (visibility_ != Visibility::Visible) return;
    for (const auto& child : children_) {
        child->paint(out, text_engine);
    }
}

LinearLayout::LinearLayout(std::string id)
    : ViewGroup(std::move(id)) {}

void LinearLayout::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }

    const float inner_w = width.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, width.size - padding_.left - padding_.right);
    const float inner_h = height.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, height.size - padding_.top - padding_.bottom);

    float major = 0.0f;
    float cross = 0.0f;
    int visible_count = 0;

    for (auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        child->measure(child_spec(inner_w, lp.width, lp.margin.left, lp.margin.right),
                       child_spec(inner_h, lp.height, lp.margin.top, lp.margin.bottom),
                       text_engine);

        const Vec2 m = child->measured_size();
        if (orientation_ == Orientation::Horizontal) {
            major += m.x + lp.margin.left + lp.margin.right;
            cross = std::max(cross, m.y + lp.margin.top + lp.margin.bottom);
        } else {
            major += m.y + lp.margin.top + lp.margin.bottom;
            cross = std::max(cross, m.x + lp.margin.left + lp.margin.right);
        }
        ++visible_count;
    }

    if (visible_count > 1) {
        major += spacing_ * static_cast<float>(visible_count - 1);
    }

    const float desired_w = orientation_ == Orientation::Horizontal
        ? major + padding_.left + padding_.right
        : cross + padding_.left + padding_.right;
    const float desired_h = orientation_ == Orientation::Horizontal
        ? cross + padding_.top + padding_.bottom
        : major + padding_.top + padding_.bottom;

    measured_size_.x = resolve_axis(layout_params_.width, width, desired_w);
    measured_size_.y = resolve_axis(layout_params_.height, height, desired_h);
}

void LinearLayout::layout(Rect bounds) {
    View::layout(bounds);
    float cursor = orientation_ == Orientation::Horizontal
        ? bounds.pos.x + padding_.left
        : bounds.pos.y + padding_.top;

    for (auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        Vec2 pos{};
        if (orientation_ == Orientation::Horizontal) {
            cursor += lp.margin.left;
            pos = {cursor, bounds.pos.y + padding_.top + lp.margin.top};
            cursor += child->measured_size().x + lp.margin.right + spacing_;
        } else {
            cursor += lp.margin.top;
            pos = {bounds.pos.x + padding_.left + lp.margin.left, cursor};
            cursor += child->measured_size().y + lp.margin.bottom + spacing_;
        }
        child->layout({.pos = pos, .size = child->measured_size()});
    }
}

FrameLayout::FrameLayout(std::string id)
    : ViewGroup(std::move(id)) {}

void FrameLayout::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    const float inner_w = width.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, width.size - padding_.left - padding_.right);
    const float inner_h = height.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, height.size - padding_.top - padding_.bottom);

    float max_w = 0.0f;
    float max_h = 0.0f;
    for (auto& child : children_) {
        const auto& lp = child->layout_params();
        child->measure(child_spec(inner_w, lp.width, lp.margin.left, lp.margin.right),
                       child_spec(inner_h, lp.height, lp.margin.top, lp.margin.bottom),
                       text_engine);
        max_w = std::max(max_w, child->measured_size().x + lp.margin.left + lp.margin.right);
        max_h = std::max(max_h, child->measured_size().y + lp.margin.top + lp.margin.bottom);
    }
    measured_size_.x = resolve_axis(layout_params_.width, width,
                                    max_w + padding_.left + padding_.right);
    measured_size_.y = resolve_axis(layout_params_.height, height,
                                    max_h + padding_.top + padding_.bottom);
}

void FrameLayout::layout(Rect bounds) {
    View::layout(bounds);
    for (auto& child : children_) {
        const auto& lp = child->layout_params();
        child->layout({
            .pos = {bounds.pos.x + padding_.left + lp.margin.left,
                    bounds.pos.y + padding_.top + lp.margin.top},
            .size = child->measured_size(),
        });
    }
}

Animation::Animation(float from, float to, float duration_s, Setter setter)
    : from_(from),
      to_(to),
      duration_s_(std::max(duration_s, 0.0001f)),
      setter_(std::move(setter)) {}

bool Animation::tick(float dt) {
    if (finished_) return true;
    elapsed_s_ = std::min(duration_s_, elapsed_s_ + std::max(0.0f, dt));
    const float t = elapsed_s_ / duration_s_;
    const float eased = t * t * (3.0f - 2.0f * t);
    if (setter_) setter_(from_ + (to_ - from_) * eased);
    finished_ = elapsed_s_ >= duration_s_;
    return finished_;
}

void Animator::add(Animation animation) {
    animations_.push_back(std::move(animation));
}

void Animator::update(float dt) {
    for (auto& animation : animations_) {
        animation.tick(dt);
    }
    animations_.erase(
        std::remove_if(animations_.begin(), animations_.end(),
                       [](const Animation& a) { return a.finished(); }),
        animations_.end());
}

UiRuntime::UiRuntime()
    : text_engine_(TextEngineConfig{}) {}

UiFrameResult UiRuntime::build_frame(View& root, Vec2 viewport) {
    root.measure({.size = viewport.x, .mode = MeasureMode::Exactly},
                 {.size = viewport.y, .mode = MeasureMode::Exactly},
                 text_engine_);
    root.layout({.pos = {0.0f, 0.0f}, .size = viewport});

    UiFrameResult result;
    root.paint(result.commands, text_engine_);
    result.draw_data = renderer_.build_draw_data(result.commands);
    return result;
}

}  // namespace snt::ui
