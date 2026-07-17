// Retained-MUI clipboard and native text-input platform bridge.
//
// This module owns host service lifetime and low-frequency diagnostics. Input
// routing supplies semantic keys and stable retained IDs; no platform event or
// retained View pointer crosses this boundary.

#define SNT_LOG_CHANNEL "ui.text_input"
#include "retained_mui_text_input_service.h"

#include "core/log.h"
#include "retained_mui_view.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace snt::ui {

void UiTextInputService::set_clipboard(std::shared_ptr<IUiClipboard> clipboard) {
    clipboard_ = std::move(clipboard);
    clipboard_read_failure_logged_ = false;
    clipboard_write_failure_logged_ = false;
}

void UiTextInputService::set_text_input_platform(
    std::shared_ptr<IUiTextInputPlatform> platform) {
    if (text_input_platform_ == platform) return;
    if (text_input_platform_ && text_input_platform_active_) {
        if (auto result = text_input_platform_->set_text_input_active(false); !result) {
            log_text_input_platform_failure(
                "deactivate", active_root_id_, active_view_id_, result.error());
        }
    }
    text_input_platform_ = std::move(platform);
    text_input_platform_active_ = false;
    last_text_input_area_.reset();
    active_root_id_.clear();
    active_view_id_.clear();
    text_input_activation_failure_logged_ = false;
    text_input_area_failure_logged_ = false;
}

bool UiTextInputService::handle_clipboard_shortcut(TextInput& input,
                                                    UiKey key,
                                                    UiKeyModifier modifiers,
                                                    std::string_view root_id,
                                                    std::string_view view_id) {
    const bool command = has_ui_key_modifier(modifiers, UiKeyModifier::Control) ||
        has_ui_key_modifier(modifiers, UiKeyModifier::Meta);
    if (!command || !clipboard_) return false;

    switch (key) {
        case UiKey::C:
            if (auto result = input.copy_selection(*clipboard_); !result) {
                log_clipboard_failure("write", root_id, view_id, result.error());
            } else {
                clipboard_write_failure_logged_ = false;
            }
            return true;
        case UiKey::X:
            if (auto result = input.cut_selection(*clipboard_); !result) {
                log_clipboard_failure("write", root_id, view_id, result.error());
            } else {
                clipboard_write_failure_logged_ = false;
            }
            return true;
        case UiKey::V:
            if (auto result = input.paste_from(*clipboard_); !result) {
                log_clipboard_failure("read", root_id, view_id, result.error());
            } else {
                clipboard_read_failure_logged_ = false;
            }
            return true;
        default: return false;
    }
}

void UiTextInputService::synchronize_platform(const UiViewport& viewport,
                                               std::optional<Rect> focused_bounds,
                                               std::string_view root_id,
                                               std::string_view view_id,
                                               bool enabled) {
    if (!text_input_platform_) return;

    const bool should_be_active = enabled && viewport.valid() && focused_bounds.has_value();
    const std::string_view diagnostic_root = should_be_active ? root_id : active_root_id_;
    const std::string_view diagnostic_view = should_be_active ? view_id : active_view_id_;
    if (should_be_active != text_input_platform_active_) {
        if (auto result = text_input_platform_->set_text_input_active(should_be_active); !result) {
            log_text_input_platform_failure(should_be_active ? "activate" : "deactivate",
                                            diagnostic_root, diagnostic_view, result.error());
            return;
        }
        text_input_platform_active_ = should_be_active;
        text_input_activation_failure_logged_ = false;
        last_text_input_area_.reset();
        if (!should_be_active) {
            active_root_id_.clear();
            active_view_id_.clear();
        }
    }
    if (!should_be_active) return;

    active_root_id_.assign(root_id);
    active_view_id_.assign(view_id);

    const Rect& bounds = *focused_bounds;
    const Vec2 top_left = viewport.logical_to_window(bounds.pos);
    const Vec2 bottom_right = viewport.logical_to_window({
        .x = bounds.pos.x + bounds.size.x,
        .y = bounds.pos.y + bounds.size.y,
    });
    const UiTextInputArea area{
        .x = static_cast<int32_t>(std::floor(top_left.x)),
        .y = static_cast<int32_t>(std::floor(top_left.y)),
        .width = std::max(1, static_cast<int32_t>(std::ceil(bottom_right.x)) -
                              static_cast<int32_t>(std::floor(top_left.x))),
        .height = std::max(1, static_cast<int32_t>(std::ceil(bottom_right.y)) -
                               static_cast<int32_t>(std::floor(top_left.y))),
    };
    const bool unchanged = last_text_input_area_ &&
        last_text_input_area_->x == area.x && last_text_input_area_->y == area.y &&
        last_text_input_area_->width == area.width &&
        last_text_input_area_->height == area.height &&
        last_text_input_area_->cursor == area.cursor;
    if (unchanged) return;
    if (auto result = text_input_platform_->set_text_input_area(area); !result) {
        log_text_input_platform_failure("area", root_id, view_id, result.error());
        return;
    }
    last_text_input_area_ = area;
    text_input_area_failure_logged_ = false;
}

void UiTextInputService::log_clipboard_failure(std::string_view operation,
                                                std::string_view root_id,
                                                std::string_view view_id,
                                                const snt::core::Error& error) {
    bool& logged = operation == "read" ? clipboard_read_failure_logged_
                                        : clipboard_write_failure_logged_;
    if (logged) return;
    logged = true;
    SNT_LOG_WARN("MUI clipboard %.*s failed: root='%.*s' view='%.*s' error=%s",
                 static_cast<int>(operation.size()), operation.data(),
                 static_cast<int>(root_id.size()), root_id.data(),
                 static_cast<int>(view_id.size()), view_id.data(), error.format().c_str());
}

void UiTextInputService::log_text_input_platform_failure(
    std::string_view operation,
    std::string_view root_id,
    std::string_view view_id,
    const snt::core::Error& error) {
    bool& logged = operation == "area" ? text_input_area_failure_logged_
                                        : text_input_activation_failure_logged_;
    if (logged) return;
    logged = true;
    SNT_LOG_WARN("MUI text input platform %.*s failed: root='%.*s' view='%.*s' error=%s",
                 static_cast<int>(operation.size()), operation.data(),
                 static_cast<int>(root_id.size()), root_id.data(),
                 static_cast<int>(view_id.size()), view_id.data(), error.format().c_str());
}

}  // namespace snt::ui
