#define SNT_LOG_CHANNEL "ui"
#include "retained_mui_view.h"
#include "retained_mui_utf8.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace snt::ui {

using detail::decode_utf8;
using detail::utf8_boundary_at_or_before;
using detail::utf8_mask;
using detail::utf8_next_boundary;
using detail::utf8_previous_boundary;

namespace {


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
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.3f", *d);
        return buffer;
    }
    return {};
}
MeasureSpec child_spec(float parent_inner, float requested, float margin_a, float margin_b) {
    const float available = std::max(0.0f, parent_inner - margin_a - margin_b);
    if (requested > 0.0f) return {.size = requested, .mode = MeasureMode::Exactly};
    if (requested == 0.0f) return {.size = available, .mode = MeasureMode::Exactly};
    return {.size = available, .mode = MeasureMode::AtMost};
}

bool scrolls_horizontally(ScrollAxis axis) {
    return axis == ScrollAxis::Horizontal || axis == ScrollAxis::Both;
}

bool scrolls_vertically(ScrollAxis axis) {
    return axis == ScrollAxis::Vertical || axis == ScrollAxis::Both;
}

}  // namespace

View::View(std::string id)
    : id_(std::move(id)) {}

void View::set_background(Color color, float radius) {
    background_ = DrawRectCommand{{}, color, radius};
}

void View::mark_layout_dirty() {
    if (layout_dirty_) return;
    layout_dirty_ = true;
    if (parent_) parent_->mark_layout_dirty();
}

void View::bind_text(ViewModel& model, std::string key) {
    set_bound_text_key(key);
    auto subscription = model.bind(std::move(key), [this](std::string_view, const BindingValue& value) {
        if (auto* text = dynamic_cast<TextView*>(this)) {
            text->set_text(binding_value_to_string(value));
        }
    });
    if (subscription.connected()) bindings_.push_back(std::move(subscription));
}

void View::bind_value(ViewModel& model, std::string key, ViewModel::Observer observer) {
    auto subscription = model.bind(std::move(key), std::move(observer));
    if (subscription.connected()) bindings_.push_back(std::move(subscription));
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

void View::paint(Arc2DCommandBuffer& out, TextEngine&, const UiTheme&) const {
    if (visibility_ != Visibility::Visible) return;
    if (background_) {
        out.rect(bounds_, background_->color, background_->radius);
    }
}

UiEventReply View::on_input_event(const UiInputEvent& event) {
    // Focus loss is a retained-lifecycle event, not a new interaction. It
    // must reach a handler after the owner disabled or hid the view so IME,
    // selection, and externally-owned state can be released deterministically.
    const bool lifecycle_event = event.type == UiInputEventType::FocusLost;
    if ((!lifecycle_event && (visibility_ != Visibility::Visible || !enabled_)) ||
        !input_handler_) {
        return UiEventReply::Ignored;
    }
    return input_handler_(event);
}

bool View::hit_test(Vec2 point) const {
    if (visibility_ != Visibility::Visible || !hit_test_visible_) return false;
    return point.x >= bounds_.pos.x && point.y >= bounds_.pos.y &&
           point.x < bounds_.pos.x + bounds_.size.x &&
           point.y < bounds_.pos.y + bounds_.size.y;
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

void TextView::paint(Arc2DCommandBuffer& out,
                     TextEngine& text_engine,
                     const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible || text_.empty()) return;
    if (dirty_layout_) {
        cached_layout_ = text_engine.shape(text_, style_);
        dirty_layout_ = false;
    }
    out.text(bounds_, text_, style_, cached_layout_);
}

Button::Button(std::string id)
    : TextView(std::move(id)) {
    set_hit_test_visible(true);
    set_focusable(true);
    TextStyle style = text_style();
    style.color = {240, 245, 255, 255};
    set_text_style(style);
}

bool Button::activate() const {
    if (!enabled() || !activate_handler_) return false;
    activate_handler_();
    return true;
}

snt::core::Expected<std::string> UiMemoryClipboard::read_text() {
    return text_;
}

snt::core::Expected<void> UiMemoryClipboard::write_text(std::string_view text) {
    text_ = text;
    return {};
}

void Button::paint(Arc2DCommandBuffer& out,
                   TextEngine& text_engine,
                   const UiTheme& theme) const {
    if (visibility_ != Visibility::Visible) return;

    Color background = theme.button_normal;
    if (!enabled()) {
        background = theme.button_disabled;
    } else if (has_interaction_state(interaction_state(), UiInteractionState::Pressed)) {
        background = theme.button_pressed;
    } else if (has_interaction_state(interaction_state(), UiInteractionState::Hovered)) {
        background = theme.button_hovered;
    } else if (has_interaction_state(interaction_state(), UiInteractionState::Focused)) {
        background = theme.button_focused;
    }
    out.rect(bounds_, background, theme.button_radius);

    if (text_.empty()) return;
    if (dirty_layout_) {
        cached_layout_ = text_engine.shape(text_, style_);
        dirty_layout_ = false;
    }
    out.text(bounds_, text_, style_, cached_layout_);
}

UiEventReply Button::on_input_event(const UiInputEvent& event) {
    const UiEventReply base_reply = View::on_input_event(event);
    if (base_reply == UiEventReply::StopPropagation || event.phase != UiEventPhase::Target ||
        !enabled()) {
        return base_reply;
    }

    if (event.type == UiInputEventType::PointerDown &&
        event.pointer_button == UiPointerButton::Primary) {
        return UiEventReply::Handled;
    }
    if (event.type == UiInputEventType::PointerUp &&
        event.pointer_button == UiPointerButton::Primary) {
        if (event.activation) activate();
        return UiEventReply::Handled;
    }
    if (event.type == UiInputEventType::KeyDown &&
        (event.key == UiKey::Enter || event.key == UiKey::Space)) {
        activate();
        return UiEventReply::Handled;
    }
    return base_reply;
}

TextInput::TextInput(std::string id)
    : TextView(std::move(id)) {
    set_hit_test_visible(true);
    set_focusable(true);
}

void TextInput::set_text(std::string text) {
    replace_text(std::move(text), true, true);
}

void TextInput::set_text_silently(std::string text) {
    replace_text(std::move(text), false, true);
}

void TextInput::set_undo_limit(size_t limit) {
    undo_limit_ = limit;
    if (undo_limit_ == 0) {
        clear_history();
        return;
    }
    if (undo_history_.size() > undo_limit_) {
        undo_history_.erase(undo_history_.begin(),
                            undo_history_.begin() + (undo_history_.size() - undo_limit_));
    }
    if (redo_history_.size() > undo_limit_) {
        redo_history_.erase(redo_history_.begin(),
                            redo_history_.begin() + (redo_history_.size() - undo_limit_));
    }
}

size_t TextInput::selection_begin_byte_offset() const {
    return std::min(utf8_boundary_at_or_before(text_, cursor_),
                    utf8_boundary_at_or_before(text_, selection_anchor_));
}

size_t TextInput::selection_end_byte_offset() const {
    return std::max(utf8_boundary_at_or_before(text_, cursor_),
                    utf8_boundary_at_or_before(text_, selection_anchor_));
}

std::string TextInput::selected_text() const {
    if (password_ || !has_selection()) return {};
    const size_t begin = selection_begin_byte_offset();
    return text_.substr(begin, selection_end_byte_offset() - begin);
}

void TextInput::select_all() {
    selection_anchor_ = 0;
    cursor_ = text_.size();
    composition_.clear();
}

snt::core::Expected<void> TextInput::copy_selection(IUiClipboard& clipboard) const {
    if (password_ || !has_selection()) return {};
    return clipboard.write_text(selected_text());
}

snt::core::Expected<void> TextInput::cut_selection(IUiClipboard& clipboard) {
    if (password_ || !has_selection()) return {};
    if (auto copied = copy_selection(clipboard); !copied) return copied.error();
    erase_selection();
    return {};
}

snt::core::Expected<void> TextInput::paste_from(IUiClipboard& clipboard) {
    auto text = clipboard.read_text();
    if (!text) return text.error();
    insert_text(*text);
    return {};
}

bool TextInput::undo() {
    if (undo_history_.empty()) return false;
    if (undo_limit_ != 0) {
        redo_history_.push_back({.text = text_, .cursor = cursor_, .selection_anchor = selection_anchor_});
    }
    EditState state = std::move(undo_history_.back());
    undo_history_.pop_back();
    restore_edit_state(std::move(state), true);
    return true;
}

bool TextInput::redo() {
    if (redo_history_.empty()) return false;
    if (undo_limit_ != 0) {
        undo_history_.push_back({.text = text_, .cursor = cursor_, .selection_anchor = selection_anchor_});
    }
    EditState state = std::move(redo_history_.back());
    redo_history_.pop_back();
    restore_edit_state(std::move(state), true);
    return true;
}

void TextInput::set_multiline(bool multiline) {
    if (multiline_ == multiline) return;
    multiline_ = multiline;
    replace_text(text_, false, true);
}

void TextInput::replace_text(std::string text, bool notify, bool reset_history) {
    text = normalize_text(text);
    if (text.size() > max_bytes_) {
        text.resize(utf8_boundary_at_or_before(text, max_bytes_));
    }
    const bool changed = text_ != text;
    if (changed) TextView::set_text(std::move(text));
    cursor_ = text_.size();
    selection_anchor_ = cursor_;
    composition_.clear();
    if (reset_history && changed) clear_history();
    if (changed && notify && change_handler_) change_handler_(text_);
}

bool TextInput::apply_user_edit(std::string text, size_t cursor) {
    text = normalize_text(text);
    if (text.size() > max_bytes_) {
        text.resize(utf8_boundary_at_or_before(text, max_bytes_));
    }
    if (text_ == text) {
        move_cursor_to(cursor, false);
        return false;
    }
    record_undo_state();
    TextView::set_text(std::move(text));
    cursor_ = utf8_boundary_at_or_before(text_, cursor);
    selection_anchor_ = cursor_;
    composition_.clear();
    if (change_handler_) change_handler_(text_);
    return true;
}

void TextInput::restore_edit_state(EditState state, bool notify) {
    state.text = normalize_text(state.text);
    if (state.text.size() > max_bytes_) {
        state.text.resize(utf8_boundary_at_or_before(state.text, max_bytes_));
    }
    const bool changed = text_ != state.text;
    if (changed) TextView::set_text(std::move(state.text));
    cursor_ = utf8_boundary_at_or_before(text_, state.cursor);
    selection_anchor_ = utf8_boundary_at_or_before(text_, state.selection_anchor);
    composition_.clear();
    if (changed && notify && change_handler_) change_handler_(text_);
}

void TextInput::record_undo_state() {
    if (undo_limit_ == 0) return;
    if (undo_history_.size() == undo_limit_) undo_history_.erase(undo_history_.begin());
    undo_history_.push_back({.text = text_, .cursor = cursor_, .selection_anchor = selection_anchor_});
    redo_history_.clear();
}

void TextInput::clear_history() {
    undo_history_.clear();
    redo_history_.clear();
}

std::string TextInput::normalize_text(std::string_view text) const {
    std::string result;
    result.reserve(text.size());
    for (size_t index = 0; index < text.size(); ++index) {
        const char value = text[index];
        if (value == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') ++index;
            result.push_back(multiline_ ? '\n' : ' ');
        } else if (value == '\n') {
            result.push_back(multiline_ ? '\n' : ' ');
        } else {
            result.push_back(value);
        }
    }
    return result;
}

void TextInput::erase_selection() {
    if (!has_selection()) return;
    const size_t begin = selection_begin_byte_offset();
    std::string next = text_;
    next.erase(begin, selection_end_byte_offset() - begin);
    apply_user_edit(std::move(next), begin);
}

void TextInput::insert_text(std::string_view inserted) {
    std::string value = normalize_text(inserted);
    if (value.empty() || cursor_ > text_.size()) return;
    const size_t begin = selection_begin_byte_offset();
    const size_t end = selection_end_byte_offset();
    const size_t retained_size = text_.size() - (end - begin);
    const size_t available = max_bytes_ > retained_size ? max_bytes_ - retained_size : 0;
    if (value.size() > available) value.resize(utf8_boundary_at_or_before(value, available));
    if (value.empty()) return;
    std::string next = text_;
    next.erase(begin, end - begin);
    next.insert(begin, value);
    apply_user_edit(std::move(next), begin + value.size());
}

void TextInput::erase_previous_codepoint() {
    if (has_selection()) {
        erase_selection();
        return;
    }
    if (cursor_ == 0 || text_.empty()) return;
    const size_t end = utf8_boundary_at_or_before(text_, cursor_);
    const size_t begin = utf8_previous_boundary(text_, end);
    std::string next = text_;
    next.erase(begin, end - begin);
    apply_user_edit(std::move(next), begin);
}

void TextInput::erase_next_codepoint() {
    if (has_selection()) {
        erase_selection();
        return;
    }
    if (cursor_ >= text_.size()) return;
    const size_t begin = utf8_boundary_at_or_before(text_, cursor_);
    const size_t end = utf8_next_boundary(text_, begin);
    std::string next = text_;
    next.erase(begin, end - begin);
    apply_user_edit(std::move(next), begin);
}

void TextInput::move_cursor_to(size_t offset, bool extend_selection) {
    cursor_ = utf8_boundary_at_or_before(text_, offset);
    if (!extend_selection) selection_anchor_ = cursor_;
}

void TextInput::move_cursor_left(bool extend_selection) {
    if (!extend_selection && has_selection()) {
        move_cursor_to(selection_begin_byte_offset(), false);
        return;
    }
    move_cursor_to(utf8_previous_boundary(text_, cursor_), extend_selection);
}

void TextInput::move_cursor_right(bool extend_selection) {
    if (!extend_selection && has_selection()) {
        move_cursor_to(selection_end_byte_offset(), false);
        return;
    }
    move_cursor_to(utf8_next_boundary(text_, cursor_), extend_selection);
}

void TextInput::move_cursor_up(bool extend_selection) {
    if (!multiline_) {
        move_cursor_home(extend_selection);
        return;
    }
    const size_t cursor = utf8_boundary_at_or_before(text_, cursor_);
    const size_t current_start = cursor == 0 ? 0 :
        (text_.rfind('\n', cursor - 1) == std::string::npos ? 0 : text_.rfind('\n', cursor - 1) + 1);
    if (current_start == 0) return;
    const size_t previous_end = current_start - 1;
    const size_t previous_start = previous_end == 0 ? 0 :
        (text_.rfind('\n', previous_end - 1) == std::string::npos
            ? 0 : text_.rfind('\n', previous_end - 1) + 1);
    size_t column = 0;
    for (size_t offset = current_start; offset < cursor; offset = utf8_next_boundary(text_, offset)) {
        ++column;
    }
    size_t target = previous_start;
    while (column > 0 && target < previous_end) {
        target = utf8_next_boundary(text_, target);
        --column;
    }
    move_cursor_to(target, extend_selection);
}

void TextInput::move_cursor_down(bool extend_selection) {
    if (!multiline_) {
        move_cursor_end(extend_selection);
        return;
    }
    const size_t cursor = utf8_boundary_at_or_before(text_, cursor_);
    const size_t current_start = cursor == 0 ? 0 :
        (text_.rfind('\n', cursor - 1) == std::string::npos ? 0 : text_.rfind('\n', cursor - 1) + 1);
    const size_t current_end = text_.find('\n', current_start);
    if (current_end == std::string::npos) return;
    const size_t next_start = current_end + 1;
    const size_t next_end = text_.find('\n', next_start);
    const size_t bounded_next_end = next_end == std::string::npos ? text_.size() : next_end;
    size_t column = 0;
    for (size_t offset = current_start; offset < cursor; offset = utf8_next_boundary(text_, offset)) {
        ++column;
    }
    size_t target = next_start;
    while (column > 0 && target < bounded_next_end) {
        target = utf8_next_boundary(text_, target);
        --column;
    }
    move_cursor_to(target, extend_selection);
}

void TextInput::move_cursor_home(bool extend_selection) {
    if (!multiline_) {
        move_cursor_to(0, extend_selection);
        return;
    }
    const size_t cursor = utf8_boundary_at_or_before(text_, cursor_);
    const size_t previous_newline = cursor == 0 ? std::string::npos : text_.rfind('\n', cursor - 1);
    move_cursor_to(previous_newline == std::string::npos ? 0 : previous_newline + 1,
                   extend_selection);
}

void TextInput::move_cursor_end(bool extend_selection) {
    if (!multiline_) {
        move_cursor_to(text_.size(), extend_selection);
        return;
    }
    const size_t next_newline = text_.find('\n', utf8_boundary_at_or_before(text_, cursor_));
    move_cursor_to(next_newline == std::string::npos ? text_.size() : next_newline,
                   extend_selection);
}

std::string TextInput::display_text() const {
    std::string value = text_;
    const size_t cursor = utf8_boundary_at_or_before(value, cursor_);
    value.insert(cursor, normalize_text(composition_));
    return password_ ? utf8_mask(value) : value;
}

void TextInput::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    TextView::measure(width, height, text_engine);
    if (visibility_ == Visibility::Gone) return;
    if (layout_params_.width < 0.0f) {
        measured_size_.x = clamp_axis(std::max(120.0f, measured_size_.x + 12.0f), width);
    }
    if (layout_params_.height < 0.0f) {
        measured_size_.y = clamp_axis(std::max(30.0f, measured_size_.y + 10.0f), height);
    }
}

void TextInput::paint(Arc2DCommandBuffer& out,
                      TextEngine& text_engine,
                      const UiTheme& theme) const {
    if (visibility_ != Visibility::Visible) return;
    const bool focused = has_interaction_state(interaction_state(), UiInteractionState::Focused);
    const Color background = !enabled() ? theme.button_disabled
        : focused ? Color{31, 54, 76, 245} : Color{24, 31, 41, 238};
    out.rect(bounds_, background, theme.button_radius);
    const Rect content{
        .pos = {bounds_.pos.x + 6.0f, bounds_.pos.y + 4.0f},
        .size = {std::max(0.0f, bounds_.size.x - 12.0f),
                 std::max(0.0f, bounds_.size.y - 8.0f)},
    };
    const float line_height = std::max(style_.size_px * 1.25f, style_.size_px);
    out.push_clip(content);
    if (focused && enabled() && has_selection()) {
        const size_t selection_begin = selection_begin_byte_offset();
        const size_t selection_end = selection_end_byte_offset();
        size_t line_start = 0;
        size_t line_index = 0;
        while (line_start <= text_.size()) {
            const size_t newline = text_.find('\n', line_start);
            const size_t line_end = newline == std::string::npos ? text_.size() : newline;
            const size_t begin = std::max(selection_begin, line_start);
            const size_t end = std::min(selection_end, line_end);
            if (begin < end) {
                std::string prefix = text_.substr(line_start, begin - line_start);
                std::string selected = text_.substr(begin, end - begin);
                if (password_) {
                    prefix = utf8_mask(prefix);
                    selected = utf8_mask(selected);
                }
                const TextLayout prefix_layout = text_engine.shape(prefix, style_);
                const TextLayout selected_layout = text_engine.shape(selected, style_);
                const float x = content.pos.x + prefix_layout.size.x;
                const float width = std::max(1.0f, selected_layout.size.x);
                out.rect({.pos = {x, content.pos.y + line_height * static_cast<float>(line_index)},
                          .size = {width, line_height}},
                         {67, 116, 173, 180});
            }
            if (newline == std::string::npos) break;
            line_start = newline + 1;
            ++line_index;
        }
    }

    const std::string displayed = display_text();
    if (!displayed.empty()) {
        const TextLayout layout = text_engine.shape(displayed, style_);
        out.text(content, displayed, style_, layout);
    } else if (!placeholder_.empty()) {
        TextStyle style = style_;
        style.color = {145, 154, 168, 220};
        const TextLayout layout = text_engine.shape(placeholder_, style);
        out.text(content, placeholder_, style, layout);
    }

    if (focused && enabled()) {
        const size_t cursor = utf8_boundary_at_or_before(text_, cursor_);
        const size_t previous_newline = cursor == 0 ? std::string::npos : text_.rfind('\n', cursor - 1);
        const size_t line_start = previous_newline == std::string::npos ? 0 : previous_newline + 1;
        const size_t line_index = static_cast<size_t>(std::count(text_.begin(),
            text_.begin() + static_cast<std::ptrdiff_t>(line_start), '\n'));
        std::string prefix = text_.substr(line_start, cursor - line_start);
        if (password_) prefix = utf8_mask(prefix);
        const TextLayout prefix_layout = text_engine.shape(prefix, style_);
        const float caret_x = std::min(content.pos.x + prefix_layout.size.x,
                                       content.pos.x + content.size.x - 1.0f);
        const float caret_y = content.pos.y + line_height * static_cast<float>(line_index) + 2.0f;
        const float caret_height = std::max(1.0f, std::min(line_height - 4.0f,
            content.pos.y + content.size.y - caret_y - 2.0f));
        if (caret_height > 0.0f) {
            out.rect({.pos = {caret_x, caret_y}, .size = {1.0f, caret_height}},
                     {232, 241, 255, 255});
        }
        if (!composition_.empty()) {
            std::string composition = normalize_text(composition_);
            if (password_) composition = utf8_mask(composition);
            const TextLayout composition_layout = text_engine.shape(composition, style_);
            out.rect({.pos = {caret_x, caret_y + std::max(0.0f, line_height - 2.0f)},
                      .size = {composition_layout.size.x, 1.0f}},
                     {126, 183, 242, 255});
        }
    }
    out.pop_clip();
}

UiEventReply TextInput::on_input_event(const UiInputEvent& event) {
    const UiEventReply base_reply = View::on_input_event(event);
    const bool lifecycle_event = event.type == UiInputEventType::FocusLost;
    if (base_reply == UiEventReply::StopPropagation || event.phase != UiEventPhase::Target ||
        (!lifecycle_event && (visibility() != Visibility::Visible || !enabled()))) {
        return base_reply;
    }
    const bool extend_selection = has_ui_key_modifier(event.modifiers, UiKeyModifier::Shift);
    const bool command = has_ui_key_modifier(event.modifiers, UiKeyModifier::Control) ||
        has_ui_key_modifier(event.modifiers, UiKeyModifier::Meta);
    switch (event.type) {
        case UiInputEventType::PointerDown:
            if (event.pointer_button == UiPointerButton::Primary) {
                move_cursor_to(text_.size(), extend_selection);
                return UiEventReply::Handled;
            }
            break;
        case UiInputEventType::TextCommit:
            insert_text(event.text);
            composition_.clear();
            return UiEventReply::Handled;
        case UiInputEventType::TextComposition:
            composition_ = normalize_text(event.text);
            composition_start_ = event.composition_start;
            composition_length_ = event.composition_length;
            return UiEventReply::Handled;
        case UiInputEventType::FocusLost:
            composition_.clear();
            return UiEventReply::Handled;
        case UiInputEventType::KeyDown:
            if (command) {
                switch (event.key) {
                    case UiKey::A: select_all(); return UiEventReply::Handled;
                    case UiKey::C:
                    case UiKey::V:
                    case UiKey::X: return UiEventReply::Handled;
                    case UiKey::Z:
                        if (extend_selection) redo(); else undo();
                        return UiEventReply::Handled;
                    case UiKey::Y: redo(); return UiEventReply::Handled;
                    default: break;
                }
            }
            switch (event.key) {
                case UiKey::Backspace: erase_previous_codepoint(); return UiEventReply::Handled;
                case UiKey::Delete: erase_next_codepoint(); return UiEventReply::Handled;
                case UiKey::Left: move_cursor_left(extend_selection); return UiEventReply::Handled;
                case UiKey::Right: move_cursor_right(extend_selection); return UiEventReply::Handled;
                case UiKey::Up: move_cursor_up(extend_selection); return UiEventReply::Handled;
                case UiKey::Down: move_cursor_down(extend_selection); return UiEventReply::Handled;
                case UiKey::Home: move_cursor_home(extend_selection); return UiEventReply::Handled;
                case UiKey::End: move_cursor_end(extend_selection); return UiEventReply::Handled;
                case UiKey::Escape: composition_.clear(); return UiEventReply::Handled;
                case UiKey::Enter:
                    if (multiline_ && !command) {
                        insert_text("\n");
                    } else if (submit_handler_) {
                        submit_handler_(text_);
                    }
                    return UiEventReply::Handled;
                default: break;
            }
            break;
        default: break;
    }
    return base_reply;
}

TextEditor::TextEditor(std::string id)
    : TextInput(std::move(id)) {
    set_multiline(true);
}

void TextEditor::set_min_visible_lines(uint32_t lines) {
    lines = std::max<uint32_t>(1, lines);
    if (min_visible_lines_ == lines) return;
    min_visible_lines_ = lines;
    mark_layout_dirty();
}

void TextEditor::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    TextInput::measure(width, height, text_engine);
    if (visibility_ == Visibility::Gone || layout_params_.height >= 0.0f) return;
    const float line_height = std::max(style_.size_px * 1.25f, style_.size_px);
    measured_size_.y = clamp_axis(std::max(measured_size_.y,
                                           line_height * static_cast<float>(min_visible_lines_) + 10.0f),
                                  height);
}

Checkbox::Checkbox(std::string id)
    : TextView(std::move(id)) {
    set_hit_test_visible(true);
    set_focusable(true);
}

void Checkbox::set_checked(bool checked, bool notify) {
    if (checked_ == checked) return;
    checked_ = checked;
    if (notify && change_handler_) change_handler_(checked_);
}

void Checkbox::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    TextView::measure(width, height, text_engine);
    if (visibility_ == Visibility::Gone) return;
    if (layout_params_.width < 0.0f) {
        measured_size_.x = clamp_axis(measured_size_.x + 24.0f, width);
    }
    if (layout_params_.height < 0.0f) {
        measured_size_.y = clamp_axis(std::max(20.0f, measured_size_.y), height);
    }
}

void Checkbox::paint(Arc2DCommandBuffer& out,
                     TextEngine& text_engine,
                     const UiTheme&) const {
    if (visibility_ != Visibility::Visible) return;
    const float side = std::min(16.0f, std::max(0.0f, bounds_.size.y - 4.0f));
    const Rect box{.pos = {bounds_.pos.x + 2.0f, bounds_.pos.y + (bounds_.size.y - side) * 0.5f},
                   .size = {side, side}};
    const bool focused = has_interaction_state(interaction_state(), UiInteractionState::Focused);
    out.rect(box, checked_ ? Color{67, 133, 196, 255}
                           : focused ? Color{63, 76, 94, 255}
                                     : Color{40, 46, 57, 255}, 3.0f);
    if (checked_) {
        out.rect({.pos = {box.pos.x + 4.0f, box.pos.y + 4.0f},
                  .size = {std::max(0.0f, side - 8.0f), std::max(0.0f, side - 8.0f)}},
                 {239, 247, 255, 255}, 1.0f);
    }
    if (text_.empty()) return;
    const TextLayout layout = text_engine.shape(text_, style_);
    out.text({.pos = {bounds_.pos.x + 24.0f, bounds_.pos.y},
              .size = {std::max(0.0f, bounds_.size.x - 24.0f), bounds_.size.y}},
             text_, style_, layout);
}

UiEventReply Checkbox::on_input_event(const UiInputEvent& event) {
    const UiEventReply base_reply = View::on_input_event(event);
    if (base_reply == UiEventReply::StopPropagation || event.phase != UiEventPhase::Target ||
        !enabled()) {
        return base_reply;
    }
    if (event.type == UiInputEventType::PointerDown &&
        event.pointer_button == UiPointerButton::Primary) {
        return UiEventReply::Handled;
    }
    if ((event.type == UiInputEventType::PointerUp && event.activation &&
         event.pointer_button == UiPointerButton::Primary) ||
        (event.type == UiInputEventType::KeyDown &&
         (event.key == UiKey::Space || event.key == UiKey::Enter))) {
        set_checked(!checked_, true);
        return UiEventReply::Handled;
    }
    return base_reply;
}

Slider::Slider(std::string id)
    : View(std::move(id)) {
    set_hit_test_visible(true);
    set_focusable(true);
}

void Slider::set_range(float minimum, float maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) return;
    if (maximum < minimum) std::swap(minimum, maximum);
    minimum_ = minimum;
    maximum_ = maximum;
    set_value(value_);
}

void Slider::set_value(float value, bool notify) {
    if (!std::isfinite(value)) return;
    float clamped = std::clamp(value, minimum_, maximum_);
    if (step_ > 0.0f) {
        clamped = minimum_ + std::round((clamped - minimum_) / step_) * step_;
        clamped = std::clamp(clamped, minimum_, maximum_);
    }
    if (value_ == clamped) return;
    value_ = clamped;
    if (notify && change_handler_) change_handler_(value_);
}

float Slider::normalized_value() const {
    const float span = maximum_ - minimum_;
    return span > 0.0f ? std::clamp((value_ - minimum_) / span, 0.0f, 1.0f) : 0.0f;
}

void Slider::set_value_from_pointer(float x) {
    const float width = std::max(1.0f, bounds_.size.x - 12.0f);
    const float t = std::clamp((x - (bounds_.pos.x + 6.0f)) / width, 0.0f, 1.0f);
    set_value(minimum_ + (maximum_ - minimum_) * t, true);
}

void Slider::measure(MeasureSpec width, MeasureSpec height, TextEngine&) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, 120.0f);
    measured_size_.y = resolve_axis(layout_params_.height, height, 24.0f);
}

void Slider::paint(Arc2DCommandBuffer& out,
                   TextEngine& text_engine,
                   const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible) return;
    const float track_y = bounds_.pos.y + bounds_.size.y * 0.5f - 2.0f;
    const Rect track{.pos = {bounds_.pos.x + 6.0f, track_y},
                     .size = {std::max(0.0f, bounds_.size.x - 12.0f), 4.0f}};
    out.rect(track, {51, 59, 71, 255}, 2.0f);
    const float filled = track.size.x * normalized_value();
    out.rect({.pos = track.pos, .size = {filled, track.size.y}}, {71, 143, 207, 255}, 2.0f);
    const float knob_x = track.pos.x + filled - 5.0f;
    const Color knob = has_interaction_state(interaction_state(), UiInteractionState::Pressed)
        ? Color{219, 239, 255, 255} : Color{183, 219, 249, 255};
    out.rect({.pos = {knob_x, track_y - 5.0f}, .size = {10.0f, 14.0f}}, knob, 5.0f);
}

UiEventReply Slider::on_input_event(const UiInputEvent& event) {
    const UiEventReply base_reply = View::on_input_event(event);
    if (base_reply == UiEventReply::StopPropagation || event.phase != UiEventPhase::Target ||
        !enabled()) {
        return base_reply;
    }
    if ((event.type == UiInputEventType::PointerDown || event.type == UiInputEventType::PointerMove ||
         event.type == UiInputEventType::PointerUp) &&
        event.pointer_button != UiPointerButton::Middle &&
        event.pointer_button != UiPointerButton::Secondary) {
        set_value_from_pointer(event.pointer_position.x);
        return UiEventReply::Handled;
    }
    if (event.type == UiInputEventType::KeyDown) {
        const float delta = step_ > 0.0f ? step_ : std::max((maximum_ - minimum_) / 100.0f, 0.01f);
        if (event.key == UiKey::Left || event.key == UiKey::Down) {
            set_value(value_ - delta, true);
            return UiEventReply::Handled;
        }
        if (event.key == UiKey::Right || event.key == UiKey::Up) {
            set_value(value_ + delta, true);
            return UiEventReply::Handled;
        }
        if (event.key == UiKey::Home) {
            set_value(minimum_, true);
            return UiEventReply::Handled;
        }
        if (event.key == UiKey::End) {
            set_value(maximum_, true);
            return UiEventReply::Handled;
        }
    }
    return base_reply;
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

void ImageView::paint(Arc2DCommandBuffer& out,
                      TextEngine& text_engine,
                      const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible || image_key_.empty()) return;
    out.image(bounds_, image_key_, tint_);
}

NineSliceView::NineSliceView(std::string id)
    : View(std::move(id)) {}

void NineSliceView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    (void)text_engine;
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width,
                                    std::max(0.0f, borders_.left + borders_.right));
    measured_size_.y = resolve_axis(layout_params_.height, height,
                                    std::max(0.0f, borders_.top + borders_.bottom));
}

void NineSliceView::paint(Arc2DCommandBuffer& out,
                          TextEngine& text_engine,
                          const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible || image_key_.empty()) return;
    out.nine_slice(bounds_, image_key_, borders_, tint_);
}

SlotView::SlotView(std::string id)
    : View(std::move(id)) {
    set_hit_test_visible(true);
    set_focusable(true);
}

void SlotView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    (void)text_engine;
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }
    measured_size_.x = resolve_axis(layout_params_.width, width, 36.0f);
    measured_size_.y = resolve_axis(layout_params_.height, height, 36.0f);
}

void SlotView::paint(Arc2DCommandBuffer& out,
                     TextEngine& text_engine,
                     const UiTheme&) const {
    if (visibility_ != Visibility::Visible) return;
    const Color base = drag_hovered_ ? Color{96, 164, 112, 255}
        : state_.selected ? Color{84, 128, 176, 255}
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
    if (drag_source_) {
        out.rect(bounds_, {10, 15, 22, 118}, 3.0f);
    }
}

ViewGroup::ViewGroup(std::string id)
    : View(std::move(id)) {}

View& ViewGroup::add_child(std::unique_ptr<View> child) {
    if (!child) {
        SNT_LOG_ERROR("ViewGroup '%s' rejected a null child", id().c_str());
        return *this;
    }
    child->attach_parent(this);
    children_.push_back(std::move(child));
    mark_layout_dirty();
    return *children_.back();
}

void ViewGroup::clear_children() {
    children_.clear();
    mark_layout_dirty();
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

void ViewGroup::paint(Arc2DCommandBuffer& out,
                      TextEngine& text_engine,
                      const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible) return;
    for (const auto& child : children_) {
        child->paint(out, text_engine, theme);
    }
}

std::optional<UiDragPayload> SlotView::begin_drag(const UiDragStartContext& context) const {
    if (!enabled() || state_.item_key.empty() || state_.count <= 0) return std::nullopt;
    if (context.pointer_button != UiPointerButton::Primary &&
        context.pointer_button != UiPointerButton::Secondary) {
        return std::nullopt;
    }
    const int32_t drag_count = context.pointer_button == UiPointerButton::Secondary
        ? (state_.count + 1) / 2
        : state_.count;
    return UiDragPayload{
        .type = "snt.item",
        .resource_key = state_.item_key,
        .count = drag_count,
    };
}

bool SlotView::accepts_drop(const UiDragPayload& payload) const {
    return enabled() && payload.type == "snt.item" && !payload.resource_key.empty() &&
        payload.count > 0;
}

void SlotView::on_drag_event(const UiDragEvent& event) {
    switch (event.type) {
    case UiDragEventType::Begin:
        drag_source_ = event.source_id == id_;
        break;
    case UiDragEventType::Enter:
        drag_hovered_ = event.target_id == id_;
        break;
    case UiDragEventType::Leave:
        if (event.target_id == id_) drag_hovered_ = false;
        break;
    case UiDragEventType::Drop:
    case UiDragEventType::Cancel:
        if (event.source_id == id_) drag_source_ = false;
        if (event.target_id == id_) drag_hovered_ = false;
        break;
    }
    if (drag_handler_) drag_handler_(event);
}

FlexLayout::FlexLayout(std::string id)
    : ViewGroup(std::move(id)) {}

void FlexLayout::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }

    const float inner_w = width.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, width.size - padding_.left - padding_.right);
    const float inner_h = height.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, height.size - padding_.top - padding_.bottom);

    const bool horizontal = orientation_ == Orientation::Horizontal;
    const float inner_major = horizontal ? inner_w : inner_h;
    const bool major_constrained = (horizontal ? width.mode : height.mode) !=
        MeasureMode::Unspecified;
    float major = 0.0f;
    float cross = 0.0f;
    float total_weight = 0.0f;
    int visible_count = 0;

    for (auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        const float requested_major = horizontal ? lp.width : lp.height;
        const float requested_cross = horizontal ? lp.height : lp.width;
        const float major_margin_before = horizontal ? lp.margin.left : lp.margin.top;
        const float major_margin_after = horizontal ? lp.margin.right : lp.margin.bottom;
        const float cross_margin_before = horizontal ? lp.margin.top : lp.margin.left;
        const float cross_margin_after = horizontal ? lp.margin.bottom : lp.margin.right;
        // A weighted match-parent axis has a zero flex basis. Wrap-content
        // keeps its measured basis and receives only the remaining space.
        const MeasureSpec major_spec = lp.weight > 0.0f && requested_major == 0.0f &&
                                       major_constrained
            ? MeasureSpec{.size = 0.0f, .mode = MeasureMode::Exactly}
            : child_spec(inner_major, requested_major, major_margin_before, major_margin_after);
        const MeasureSpec cross_spec = child_spec(horizontal ? inner_h : inner_w,
                                                  requested_cross,
                                                  cross_margin_before,
                                                  cross_margin_after);
        child->measure(horizontal ? major_spec : cross_spec,
                       horizontal ? cross_spec : major_spec,
                       text_engine);

        const Vec2 m = child->measured_size();
        if (horizontal) {
            major += m.x + lp.margin.left + lp.margin.right;
            cross = std::max(cross, m.y + lp.margin.top + lp.margin.bottom);
        } else {
            major += m.y + lp.margin.top + lp.margin.bottom;
            cross = std::max(cross, m.x + lp.margin.left + lp.margin.right);
        }
        total_weight += std::max(0.0f, lp.weight);
        ++visible_count;
    }

    if (visible_count > 1) {
        major += spacing_ * static_cast<float>(visible_count - 1);
    }

    // Flex grow is resolved while measuring, so subsequent layout has stable
    // child sizes and does not shift hit regions after a pointer event.
    const float remaining = major_constrained ? std::max(0.0f, inner_major - major) : 0.0f;
    if (remaining > 0.0f && total_weight > 0.0f) {
        for (auto& child : children_) {
            if (child->visibility() == Visibility::Gone) continue;
            const auto& lp = child->layout_params();
            if (lp.weight <= 0.0f) continue;
            const float major_margin_before = horizontal ? lp.margin.left : lp.margin.top;
            const float major_margin_after = horizontal ? lp.margin.right : lp.margin.bottom;
            const float target_outer = (horizontal ? child->measured_size().x
                                                   : child->measured_size().y) +
                remaining * lp.weight / total_weight;
            const MeasureSpec grown_major{
                .size = std::max(0.0f, target_outer - major_margin_before - major_margin_after),
                .mode = MeasureMode::Exactly,
            };
            const float requested_cross = horizontal ? lp.height : lp.width;
            const float cross_margin_before = horizontal ? lp.margin.top : lp.margin.left;
            const float cross_margin_after = horizontal ? lp.margin.bottom : lp.margin.right;
            const MeasureSpec cross_spec = child_spec(horizontal ? inner_h : inner_w,
                                                      requested_cross,
                                                      cross_margin_before,
                                                      cross_margin_after);
            child->measure(horizontal ? grown_major : cross_spec,
                           horizontal ? cross_spec : grown_major,
                           text_engine);
        }
        major += remaining;
    }

    const float desired_w = horizontal
        ? major + padding_.left + padding_.right
        : cross + padding_.left + padding_.right;
    const float desired_h = horizontal
        ? cross + padding_.top + padding_.bottom
        : major + padding_.top + padding_.bottom;

    measured_size_.x = resolve_axis(layout_params_.width, width, desired_w);
    measured_size_.y = resolve_axis(layout_params_.height, height, desired_h);
}

void FlexLayout::layout(Rect bounds) {
    View::layout(bounds);
    const bool horizontal = orientation_ == Orientation::Horizontal;
    const float inner_major = std::max(0.0f, (horizontal ? bounds.size.x : bounds.size.y) -
                                       (horizontal ? padding_.left + padding_.right
                                                   : padding_.top + padding_.bottom));
    const float inner_cross = std::max(0.0f, (horizontal ? bounds.size.y : bounds.size.x) -
                                       (horizontal ? padding_.top + padding_.bottom
                                                   : padding_.left + padding_.right));
    float occupied = 0.0f;
    int visible_count = 0;
    for (const auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        occupied += (horizontal ? child->measured_size().x + lp.margin.left + lp.margin.right
                                : child->measured_size().y + lp.margin.top + lp.margin.bottom);
        ++visible_count;
    }
    if (visible_count > 1) occupied += spacing_ * static_cast<float>(visible_count - 1);
    const float free = std::max(0.0f, inner_major - occupied);
    float leading = 0.0f;
    float gap = spacing_;
    switch (justify_) {
        case FlexJustify::Start: break;
        case FlexJustify::Center: leading = free * 0.5f; break;
        case FlexJustify::End: leading = free; break;
        case FlexJustify::SpaceBetween:
            if (visible_count > 1) gap += free / static_cast<float>(visible_count - 1);
            break;
        case FlexJustify::SpaceAround:
            if (visible_count > 0) {
                const float space = free / static_cast<float>(visible_count);
                leading = space * 0.5f;
                gap += space;
            }
            break;
        case FlexJustify::SpaceEvenly:
            if (visible_count > 0) {
                const float space = free / static_cast<float>(visible_count + 1);
                leading = space;
                gap += space;
            }
            break;
    }
    float cursor = (horizontal ? bounds.pos.x + padding_.left : bounds.pos.y + padding_.top) +
        leading;

    for (auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        const float major_margin_before = horizontal ? lp.margin.left : lp.margin.top;
        const float major_margin_after = horizontal ? lp.margin.right : lp.margin.bottom;
        const float cross_margin_before = horizontal ? lp.margin.top : lp.margin.left;
        const float cross_margin_after = horizontal ? lp.margin.bottom : lp.margin.right;
        const float requested_cross = horizontal ? lp.height : lp.width;
        const float measured_cross = horizontal ? child->measured_size().y : child->measured_size().x;
        const float child_cross = align_ == FlexAlign::Stretch && requested_cross <= 0.0f
            ? std::max(0.0f, inner_cross - cross_margin_before - cross_margin_after)
            : measured_cross;
        const float remaining_cross = std::max(0.0f,
            inner_cross - child_cross - cross_margin_before - cross_margin_after);
        float cross_offset = cross_margin_before;
        if (align_ == FlexAlign::Center) cross_offset += remaining_cross * 0.5f;
        if (align_ == FlexAlign::End) cross_offset += remaining_cross;
        cursor += major_margin_before;
        const Vec2 pos = horizontal
            ? Vec2{cursor, bounds.pos.y + padding_.top + cross_offset}
            : Vec2{bounds.pos.x + padding_.left + cross_offset, cursor};
        const Vec2 size = horizontal
            ? Vec2{child->measured_size().x, child_cross}
            : Vec2{child_cross, child->measured_size().y};
        child->layout({.pos = pos, .size = size});
        cursor += (horizontal ? size.x : size.y) + major_margin_after + gap;
    }
}

GridLayout::GridLayout(std::string id)
    : ViewGroup(std::move(id)) {}

void GridLayout::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        return;
    }

    const int32_t safe_columns = std::max(1, columns_);
    const float inner_w = width.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, width.size - padding_.left - padding_.right);
    const float inner_h = height.mode == MeasureMode::Unspecified
        ? 0.0f : std::max(0.0f, height.size - padding_.top - padding_.bottom);
    const int32_t visible_children = static_cast<int32_t>(std::count_if(
        children_.begin(), children_.end(), [](const std::unique_ptr<View>& child) {
            return child->visibility() != Visibility::Gone;
        }));
    const int32_t row_count = visible_children == 0
        ? 0 : (visible_children + safe_columns - 1) / safe_columns;
    const float cell_w = width.mode == MeasureMode::Unspecified
        ? 0.0f
        : std::max(0.0f, (inner_w - column_spacing_ * static_cast<float>(safe_columns - 1)) /
                            static_cast<float>(safe_columns));
    const float cell_h = height.mode == MeasureMode::Unspecified || row_count == 0
        ? 0.0f
        : std::max(0.0f, (inner_h - row_spacing_ * static_cast<float>(row_count - 1)) /
                            static_cast<float>(row_count));

    std::vector<float> column_widths(static_cast<size_t>(safe_columns), 0.0f);
    std::vector<float> row_heights;
    int32_t visible_index = 0;
    for (auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        const MeasureSpec child_width = width.mode == MeasureMode::Unspecified
            ? MeasureSpec{}
            : child_spec(cell_w, lp.width, lp.margin.left, lp.margin.right);
        const MeasureSpec child_height = height.mode == MeasureMode::Unspecified
            ? MeasureSpec{}
            : child_spec(cell_h, lp.height, lp.margin.top, lp.margin.bottom);
        child->measure(child_width, child_height, text_engine);

        const int32_t column = visible_index % safe_columns;
        const int32_t row = visible_index / safe_columns;
        if (row >= static_cast<int32_t>(row_heights.size())) row_heights.push_back(0.0f);
        column_widths[static_cast<size_t>(column)] = std::max(
            column_widths[static_cast<size_t>(column)],
            child->measured_size().x + lp.margin.left + lp.margin.right);
        row_heights[static_cast<size_t>(row)] = std::max(
            row_heights[static_cast<size_t>(row)],
            child->measured_size().y + lp.margin.top + lp.margin.bottom);
        ++visible_index;
    }

    float content_width = 0.0f;
    for (float column_width : column_widths) content_width += column_width;
    if (visible_index > 0) {
        const int32_t used_columns = std::min(safe_columns, visible_index);
        content_width += column_spacing_ * static_cast<float>(used_columns - 1);
    }
    float content_height = 0.0f;
    for (float row_height : row_heights) content_height += row_height;
    if (row_heights.size() > 1) {
        content_height += row_spacing_ * static_cast<float>(row_heights.size() - 1);
    }

    measured_size_.x = resolve_axis(layout_params_.width, width,
                                    content_width + padding_.left + padding_.right);
    measured_size_.y = resolve_axis(layout_params_.height, height,
                                    content_height + padding_.top + padding_.bottom);
}

void GridLayout::layout(Rect bounds) {
    View::layout(bounds);
    const int32_t safe_columns = std::max(1, columns_);
    std::vector<float> column_widths(static_cast<size_t>(safe_columns), 0.0f);
    std::vector<float> row_heights;
    int32_t visible_index = 0;
    for (const auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        const int32_t column = visible_index % safe_columns;
        const int32_t row = visible_index / safe_columns;
        if (row >= static_cast<int32_t>(row_heights.size())) row_heights.push_back(0.0f);
        column_widths[static_cast<size_t>(column)] = std::max(
            column_widths[static_cast<size_t>(column)],
            child->measured_size().x + lp.margin.left + lp.margin.right);
        row_heights[static_cast<size_t>(row)] = std::max(
            row_heights[static_cast<size_t>(row)],
            child->measured_size().y + lp.margin.top + lp.margin.bottom);
        ++visible_index;
    }

    std::vector<float> column_offsets(static_cast<size_t>(safe_columns),
                                      bounds.pos.x + padding_.left);
    for (int32_t column = 1; column < safe_columns; ++column) {
        column_offsets[static_cast<size_t>(column)] =
            column_offsets[static_cast<size_t>(column - 1)] +
            column_widths[static_cast<size_t>(column - 1)] + column_spacing_;
    }
    std::vector<float> row_offsets(row_heights.size(), bounds.pos.y + padding_.top);
    for (size_t row = 1; row < row_offsets.size(); ++row) {
        row_offsets[row] = row_offsets[row - 1] + row_heights[row - 1] + row_spacing_;
    }

    visible_index = 0;
    for (auto& child : children_) {
        if (child->visibility() == Visibility::Gone) continue;
        const auto& lp = child->layout_params();
        const int32_t column = visible_index % safe_columns;
        const int32_t row = visible_index / safe_columns;
        child->layout({
            .pos = {column_offsets[static_cast<size_t>(column)] + lp.margin.left,
                    row_offsets[static_cast<size_t>(row)] + lp.margin.top},
            .size = child->measured_size(),
        });
        ++visible_index;
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

ScrollView::ScrollView(std::string id)
    : ViewGroup(std::move(id)) {
    set_hit_test_visible(true);
}

View& ScrollView::set_content(std::unique_ptr<View> content_view) {
    if (!content_view) {
        SNT_LOG_ERROR("ScrollView '%s' rejected null content", id().c_str());
        return *this;
    }
    clear_children();
    return add_child(std::move(content_view));
}

View* ScrollView::content() {
    return children_.empty() ? nullptr : children_.front().get();
}

const View* ScrollView::content() const {
    return children_.empty() ? nullptr : children_.front().get();
}

void ScrollView::set_scroll_axis(ScrollAxis axis) {
    scroll_axis_ = axis;
    clamp_scroll_offset();
    layout_content();
}

void ScrollView::set_scroll_step(float pixels) {
    scroll_step_ = std::max(1.0f, pixels);
}

void ScrollView::set_scroll_offset(Vec2 offset) {
    scroll_offset_ = offset;
    clamp_scroll_offset();
    layout_content();
}

void ScrollView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        content_size_ = {};
        max_scroll_offset_ = {};
        return;
    }

    View* const content_view = content();
    if (!content_view || content_view->visibility() == Visibility::Gone) {
        content_size_ = {};
        measured_size_.x = resolve_axis(layout_params_.width, width, 0.0f);
        measured_size_.y = resolve_axis(layout_params_.height, height, 0.0f);
        max_scroll_offset_ = {};
        scroll_offset_ = {};
        return;
    }

    const auto& content_params = content_view->layout_params();
    const bool horizontal = scrolls_horizontally(scroll_axis_);
    const bool vertical = scrolls_vertically(scroll_axis_);
    const float available_width = width.mode == MeasureMode::Unspecified
        ? 0.0f
        : std::max(0.0f, width.size - content_params.margin.left - content_params.margin.right);
    const float available_height = height.mode == MeasureMode::Unspecified
        ? 0.0f
        : std::max(0.0f, height.size - content_params.margin.top - content_params.margin.bottom);

    const MeasureSpec content_width = horizontal
        ? MeasureSpec{}
        : MeasureSpec{.size = available_width,
                      .mode = width.mode == MeasureMode::Unspecified
                          ? MeasureMode::Unspecified : MeasureMode::Exactly};
    const MeasureSpec content_height = vertical
        ? MeasureSpec{}
        : MeasureSpec{.size = available_height,
                      .mode = height.mode == MeasureMode::Unspecified
                          ? MeasureMode::Unspecified : MeasureMode::Exactly};
    content_view->measure(content_width, content_height, text_engine);
    const Vec2 measured_content = content_view->measured_size();
    content_size_ = {
        measured_content.x + content_params.margin.left + content_params.margin.right,
        measured_content.y + content_params.margin.top + content_params.margin.bottom,
    };

    measured_size_.x = resolve_axis(layout_params_.width, width, content_size_.x);
    measured_size_.y = resolve_axis(layout_params_.height, height, content_size_.y);
}

void ScrollView::layout(Rect bounds) {
    View::layout(bounds);
    layout_content();
}

void ScrollView::paint(Arc2DCommandBuffer& out,
                       TextEngine& text_engine,
                       const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible || children_.empty()) return;
    out.push_clip(bounds_);
    for (const auto& child : children_) {
        child->paint(out, text_engine, theme);
    }
    out.pop_clip();
}

UiEventReply ScrollView::on_input_event(const UiInputEvent& event) {
    const UiEventReply base_reply = View::on_input_event(event);
    if (base_reply == UiEventReply::StopPropagation || !enabled() ||
        visibility() != Visibility::Visible || event.type != UiInputEventType::PointerScroll ||
        (event.phase != UiEventPhase::Target && event.phase != UiEventPhase::Bubble)) {
        return base_reply;
    }

    Vec2 delta{};
    if (scrolls_horizontally(scroll_axis_)) {
        delta.x = -event.scroll_delta.x * scroll_step_;
    }
    if (scrolls_vertically(scroll_axis_)) {
        delta.y = -event.scroll_delta.y * scroll_step_;
    }
    // The nearest viewport that can move owns this wheel step. Returning
    // StopPropagation keeps nested scroll panes from moving their parents at
    // the same time; once this viewport reaches its edge, Ignored lets an
    // ancestor ScrollView consume the remaining input naturally.
    return scroll_by(delta) ? UiEventReply::StopPropagation : base_reply;
}

bool ScrollView::accepts_child_input(Vec2 point) const {
    return point.x >= bounds_.pos.x && point.y >= bounds_.pos.y &&
           point.x < bounds_.pos.x + bounds_.size.x &&
           point.y < bounds_.pos.y + bounds_.size.y;
}

void ScrollView::layout_content() {
    View* const content_view = content();
    if (!content_view || content_view->visibility() == Visibility::Gone) {
        max_scroll_offset_ = {};
        scroll_offset_ = {};
        return;
    }

    const auto& content_params = content_view->layout_params();
    const Vec2 viewport = {
        std::max(0.0f, bounds_.size.x),
        std::max(0.0f, bounds_.size.y),
    };
    content_size_ = {
        content_view->measured_size().x + content_params.margin.left + content_params.margin.right,
        content_view->measured_size().y + content_params.margin.top + content_params.margin.bottom,
    };
    max_scroll_offset_ = {
        scrolls_horizontally(scroll_axis_)
            ? std::max(0.0f, content_size_.x - viewport.x) : 0.0f,
        scrolls_vertically(scroll_axis_)
            ? std::max(0.0f, content_size_.y - viewport.y) : 0.0f,
    };
    clamp_scroll_offset();
    content_view->layout({
        .pos = {bounds_.pos.x + content_params.margin.left - scroll_offset_.x,
                bounds_.pos.y + content_params.margin.top - scroll_offset_.y},
        .size = content_view->measured_size(),
    });
}

void ScrollView::clamp_scroll_offset() {
    scroll_offset_.x = std::clamp(scroll_offset_.x, 0.0f, max_scroll_offset_.x);
    scroll_offset_.y = std::clamp(scroll_offset_.y, 0.0f, max_scroll_offset_.y);
}

bool ScrollView::scroll_by(Vec2 delta) {
    const Vec2 before = scroll_offset_;
    scroll_offset_.x += delta.x;
    scroll_offset_.y += delta.y;
    clamp_scroll_offset();
    const bool changed = before.x != scroll_offset_.x || before.y != scroll_offset_.y;
    if (changed) layout_content();
    return changed;
}

VirtualListView::VirtualListView(std::string id)
    : ViewGroup(std::move(id)) {
    set_hit_test_visible(true);
}

void VirtualListView::set_item_count(size_t count) {
    if (item_count_ == count) return;
    item_count_ = count;
    mark_layout_dirty();
}

void VirtualListView::set_item_extent(float extent) {
    const float clamped = std::max(1.0f, extent);
    if (item_extent_ == clamped) return;
    item_extent_ = clamped;
    mark_layout_dirty();
}

void VirtualListView::set_item_builder(ItemBuilder builder) {
    item_builder_ = std::move(builder);
    children_.clear();
    first_realized_ = 0;
    mark_layout_dirty();
}

void VirtualListView::set_scroll_offset(float offset) {
    scroll_offset_ = offset;
    clamp_scroll_offset();
    mark_layout_dirty();
}

void VirtualListView::measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) {
    if (visibility_ == Visibility::Gone) {
        measured_size_ = {};
        children_.clear();
        return;
    }
    const float desired_width = 120.0f;
    const float desired_height = std::min(
        static_cast<float>(item_count_) * item_extent_, 240.0f);
    measured_size_.x = resolve_axis(layout_params_.width, width, desired_width);
    measured_size_.y = resolve_axis(layout_params_.height, height, desired_height);
    viewport_height_ = std::max(0.0f, measured_size_.y);
    max_scroll_offset_ = std::max(0.0f, static_cast<float>(item_count_) * item_extent_ -
                                            viewport_height_);
    clamp_scroll_offset();
    realize_visible(text_engine, measured_size_.x);
}

void VirtualListView::realize_visible(TextEngine& text_engine, float available_width) {
    if (!item_builder_ || item_count_ == 0) {
        children_.clear();
        first_realized_ = 0;
        return;
    }
    const size_t first = std::min(item_count_ - 1,
        static_cast<size_t>(std::floor(scroll_offset_ / item_extent_)));
    const size_t visible = static_cast<size_t>(std::ceil(viewport_height_ / item_extent_));
    const size_t begin = first > 1 ? first - 1 : 0;
    const size_t end = std::min(item_count_, first + visible + 2);
    const size_t count = end > begin ? end - begin : 0;
    const bool matches = first_realized_ == begin && children_.size() == count;
    if (!matches) {
        children_.clear();
        first_realized_ = begin;
        for (size_t index = begin; index < end; ++index) {
            std::unique_ptr<View> child = item_builder_(index);
            if (!child) {
                SNT_LOG_WARN("VirtualListView '%s' item builder returned null at index %zu",
                             id().c_str(), index);
                continue;
            }
            LayoutParams params = child->layout_params();
            params.width = 0.0f;
            params.height = item_extent_;
            params.margin = {};
            child->set_layout_params(params);
            add_child(std::move(child));
        }
    }
    for (auto& child : children_) {
        child->measure({.size = std::max(0.0f, available_width), .mode = MeasureMode::Exactly},
                       {.size = item_extent_, .mode = MeasureMode::Exactly}, text_engine);
    }
}

void VirtualListView::layout(Rect bounds) {
    View::layout(bounds);
    viewport_height_ = std::max(0.0f, bounds.size.y);
    max_scroll_offset_ = std::max(0.0f, static_cast<float>(item_count_) * item_extent_ -
                                            viewport_height_);
    clamp_scroll_offset();
    for (size_t index = 0; index < children_.size(); ++index) {
        const float y = bounds.pos.y +
            static_cast<float>(first_realized_ + index) * item_extent_ - scroll_offset_;
        children_[index]->layout({.pos = {bounds.pos.x, y},
                                  .size = {std::max(0.0f, bounds.size.x), item_extent_}});
    }
}

void VirtualListView::paint(Arc2DCommandBuffer& out,
                            TextEngine& text_engine,
                            const UiTheme& theme) const {
    View::paint(out, text_engine, theme);
    if (visibility_ != Visibility::Visible) return;
    out.push_clip(bounds_);
    for (const auto& child : children_) child->paint(out, text_engine, theme);
    out.pop_clip();
}

UiEventReply VirtualListView::on_input_event(const UiInputEvent& event) {
    const UiEventReply base_reply = View::on_input_event(event);
    if (base_reply == UiEventReply::StopPropagation || !enabled() ||
        visibility() != Visibility::Visible || event.type != UiInputEventType::PointerScroll ||
        (event.phase != UiEventPhase::Target && event.phase != UiEventPhase::Bubble)) {
        return base_reply;
    }
    return scroll_by(-event.scroll_delta.y * scroll_step_)
        ? UiEventReply::StopPropagation : base_reply;
}

bool VirtualListView::accepts_child_input(Vec2 point) const {
    return point.x >= bounds_.pos.x && point.y >= bounds_.pos.y &&
           point.x < bounds_.pos.x + bounds_.size.x &&
           point.y < bounds_.pos.y + bounds_.size.y;
}

void VirtualListView::clamp_scroll_offset() {
    scroll_offset_ = std::clamp(scroll_offset_, 0.0f, max_scroll_offset_);
}

bool VirtualListView::scroll_by(float delta) {
    const float before = scroll_offset_;
    scroll_offset_ += delta;
    clamp_scroll_offset();
    const bool changed = before != scroll_offset_;
    if (changed) mark_layout_dirty();
    return changed;
}

ModalView::ModalView(std::string id)
    : FrameLayout(std::move(id)) {
    set_hit_test_visible(true);
    set_focusable(true);
}

void ModalView::paint(Arc2DCommandBuffer& out,
                      TextEngine& text_engine,
                      const UiTheme& theme) const {
    if (visibility_ != Visibility::Visible) return;
    out.rect(bounds_, backdrop_);
    ViewGroup::paint(out, text_engine, theme);
}

UiEventReply ModalView::on_input_event(const UiInputEvent& event) {
    const UiEventReply base_reply = View::on_input_event(event);
    if (base_reply == UiEventReply::StopPropagation || event.phase != UiEventPhase::Target ||
        !enabled()) {
        return base_reply;
    }
    if (event.type == UiInputEventType::PointerDown &&
        event.pointer_button == UiPointerButton::Primary) {
        if (dismiss_on_backdrop_ && dismiss_handler_) dismiss_handler_();
        return UiEventReply::Handled;
    }
    if (event.type == UiInputEventType::KeyDown && event.key == UiKey::Escape &&
        dismiss_handler_) {
        dismiss_handler_();
        return UiEventReply::Handled;
    }
    return base_reply;
}

TooltipView::TooltipView(std::string id)
    : TextView(std::move(id)) {
    set_hit_test_visible(false);
    set_background({22, 27, 35, 245}, 4.0f);
    TextStyle style = text_style();
    style.size_px = 13.0f;
    set_text_style(style);
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

}  // namespace snt::ui
