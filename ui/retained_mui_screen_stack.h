// Retained-MUI screen registration, mounting, and layer lifecycle.

#pragma once

#include "retained_mui_view.h"
#include "ui_image_registry.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace snt::ui {

using UiActionDispatcher = std::function<void(std::string_view action_id)>;

struct UiScreenMountContext {
    Vec2 viewport{};
    UiViewport ui_viewport{};
    UiImageRegistry& images;
    UiActionDispatcher dispatch_action;
};

struct UiScreenFrameContext {
    Vec2 viewport{};
    UiViewport ui_viewport{};
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

// Engine-facing lifecycle hook. It runs synchronously while the retained root
// is still owned by UiLayerStack, before hide, replacement, or destruction.
// The handler must not mutate the stack that is invoking it.
using UiRetainedRootInvalidator = std::function<void(View& root)>;

class UiLayerStack final {
public:
    void set_retained_root_invalidator(UiRetainedRootInvalidator invalidator) {
        retained_root_invalidator_ = std::move(invalidator);
    }
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
    UiRetainedRootInvalidator retained_root_invalidator_;
};

}  // namespace snt::ui
