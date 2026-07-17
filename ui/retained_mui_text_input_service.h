// Retained-MUI clipboard and native text-input platform bridge.

#pragma once

#include "retained_mui_types.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace snt::ui {

class TextInput;

class UiTextInputService {
public:
    // Host services are shared ownership so retained UI never holds a dangling
    // native-platform object. They remain optional for headless tests.
    void set_clipboard(std::shared_ptr<IUiClipboard> clipboard);
    void set_text_input_platform(std::shared_ptr<IUiTextInputPlatform> platform);

    // Runs semantic clipboard shortcuts after the focused widget receives its
    // KeyDown event. Root/view ids are used only for rate-limited diagnostics.
    bool handle_clipboard_shortcut(TextInput& input,
                                   UiKey key,
                                   UiKeyModifier modifiers,
                                   std::string_view root_id,
                                   std::string_view view_id);

    // Native candidate coordinates are converted at this boundary, keeping
    // platform APIs independent from retained logical UI coordinates.
    void synchronize_platform(const UiViewport& viewport,
                              std::optional<Rect> focused_bounds,
                              std::string_view root_id,
                              std::string_view view_id,
                              bool enabled = true);

private:
    void log_clipboard_failure(std::string_view operation,
                               std::string_view root_id,
                               std::string_view view_id,
                               const snt::core::Error& error);
    void log_text_input_platform_failure(std::string_view operation,
                                         std::string_view root_id,
                                         std::string_view view_id,
                                         const snt::core::Error& error);

    std::shared_ptr<IUiClipboard> clipboard_;
    std::shared_ptr<IUiTextInputPlatform> text_input_platform_;
    std::optional<UiTextInputArea> last_text_input_area_;
    bool text_input_platform_active_ = false;
    bool clipboard_read_failure_logged_ = false;
    bool clipboard_write_failure_logged_ = false;
    bool text_input_activation_failure_logged_ = false;
    bool text_input_area_failure_logged_ = false;
    std::string active_root_id_;
    std::string active_view_id_;
};

}  // namespace snt::ui
