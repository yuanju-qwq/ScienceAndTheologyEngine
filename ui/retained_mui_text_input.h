// Retained-MUI UTF-8 editable text widgets.

#pragma once

#include "retained_mui_text_view.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace snt::ui {

// UTF-8 editing base for TextInput and TextEditor. Platform text commits and
// IME preedit state arrive through UiInputEvent; clipboard and native IME
// services remain injected into UiTextInputService instead of retained widgets.
class TextInput : public TextView {
public:
    using ChangeHandler = std::function<void(std::string_view)>;
    using SubmitHandler = std::function<void(std::string_view)>;

    explicit TextInput(std::string id = {});
    ViewKind kind() const override { return ViewKind::TextInput; }

    void set_text(std::string text);
    void set_text_silently(std::string text);
    void set_placeholder(std::string placeholder) {
        placeholder_ = std::move(placeholder);
        mark_layout_dirty();
    }
    const std::string& placeholder() const { return placeholder_; }
    void set_max_bytes(size_t max_bytes) { max_bytes_ = max_bytes; }
    size_t max_bytes() const { return max_bytes_; }
    void set_password(bool password) {
        if (password_ == password) return;
        password_ = password;
        mark_layout_dirty();
    }
    bool password() const { return password_; }
    void set_undo_limit(size_t limit);
    size_t undo_limit() const { return undo_limit_; }
    void set_on_change(ChangeHandler handler) { change_handler_ = std::move(handler); }
    void set_on_submit(SubmitHandler handler) { submit_handler_ = std::move(handler); }
    size_t cursor_byte_offset() const { return cursor_; }
    size_t selection_anchor_byte_offset() const { return selection_anchor_; }
    size_t selection_begin_byte_offset() const;
    size_t selection_end_byte_offset() const;
    bool has_selection() const { return cursor_ != selection_anchor_; }
    std::string selected_text() const;
    void select_all();
    bool undo();
    bool redo();
    [[nodiscard]] snt::core::Expected<void> copy_selection(IUiClipboard& clipboard) const;
    [[nodiscard]] snt::core::Expected<void> cut_selection(IUiClipboard& clipboard);
    [[nodiscard]] snt::core::Expected<void> paste_from(IUiClipboard& clipboard);
    Rect ime_bounds() const { return bounds(); }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    UiEventReply on_input_event(const UiInputEvent& event) override;

protected:
    void set_multiline(bool multiline);
    bool multiline() const { return multiline_; }

private:
    struct EditState {
        std::string text;
        size_t cursor = 0;
        size_t selection_anchor = 0;
    };

    void replace_text(std::string text, bool notify, bool reset_history);
    bool apply_user_edit(std::string text, size_t cursor);
    void restore_edit_state(EditState state, bool notify);
    void record_undo_state();
    void clear_history();
    std::string normalize_text(std::string_view text) const;
    void erase_selection();
    void insert_text(std::string_view text);
    void erase_previous_codepoint();
    void erase_next_codepoint();
    void move_cursor_left(bool extend_selection);
    void move_cursor_right(bool extend_selection);
    void move_cursor_up(bool extend_selection);
    void move_cursor_down(bool extend_selection);
    void move_cursor_home(bool extend_selection);
    void move_cursor_end(bool extend_selection);
    void move_cursor_to(size_t offset, bool extend_selection);
    std::string display_text() const;

    std::string placeholder_;
    std::string composition_;
    int32_t composition_start_ = -1;
    int32_t composition_length_ = -1;
    size_t cursor_ = 0;
    size_t selection_anchor_ = 0;
    size_t max_bytes_ = 4096;
    size_t undo_limit_ = 128;
    std::vector<EditState> undo_history_;
    std::vector<EditState> redo_history_;
    bool multiline_ = false;
    bool password_ = false;
    ChangeHandler change_handler_;
    SubmitHandler submit_handler_;
};

// Retained multi-line editor for chat, notes, and future Mod configuration.
// It shares TextInput's UTF-8, IME, clipboard, selection, and history rules
// while Enter inserts a line break and Ctrl/Meta+Enter invokes submit.
class TextEditor final : public TextInput {
public:
    explicit TextEditor(std::string id = {});
    ViewKind kind() const override { return ViewKind::TextEditor; }

    void set_min_visible_lines(uint32_t lines);
    uint32_t min_visible_lines() const { return min_visible_lines_; }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;

private:
    uint32_t min_visible_lines_ = 3;
};

}  // namespace snt::ui
