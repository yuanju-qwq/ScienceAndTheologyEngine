// Retained-MUI screen registration and layer lifecycle.
//
// This module owns screen mount lifetime, layer input policy, and resource
// reload replacement. It stays independent from per-frame input routing.

#define SNT_LOG_CHANNEL "ui.screen_stack"
#include "retained_mui_screen_stack.h"

#include "core/log.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

namespace snt::ui {

UiLayerInputPolicy ui_layer_input_policy(UiLayer layer) {
    switch (layer) {
        case UiLayer::Hud:
        case UiLayer::Screen:
            return {};
        case UiLayer::Modal:
            return {.accepts_pointer = true,
                    .accepts_keyboard = true,
                    .blocks_pointer_below = true,
                    .blocks_keyboard_below = true};
        case UiLayer::Tooltip:
            return {.accepts_pointer = false,
                    .accepts_keyboard = false,
                    .blocks_pointer_below = false,
                    .blocks_keyboard_below = false};
    }
    return {};
}

bool UiLayerStack::valid_key_part(std::string_view value) {
    return !value.empty() && value.find(':') == std::string_view::npos;
}

std::string UiLayerStack::qualified_id(std::string_view owner_id,
                                       std::string_view screen_id) {
    return std::string(owner_id) + ":" + std::string(screen_id);
}

UiLayerStack::ScreenRecord* UiLayerStack::find_record(
    std::string_view owner_id,
    std::string_view screen_id) {
    auto found = std::find_if(screens_.begin(), screens_.end(),
        [owner_id, screen_id](const ScreenRecord& record) {
            return record.registration.owner_id == owner_id &&
                   record.registration.screen_id == screen_id;
        });
    return found == screens_.end() ? nullptr : &*found;
}

const UiLayerStack::ScreenRecord* UiLayerStack::find_record(
    std::string_view owner_id,
    std::string_view screen_id) const {
    auto found = std::find_if(screens_.begin(), screens_.end(),
        [owner_id, screen_id](const ScreenRecord& record) {
            return record.registration.owner_id == owner_id &&
                   record.registration.screen_id == screen_id;
        });
    return found == screens_.end() ? nullptr : &*found;
}

snt::core::Expected<void> UiLayerStack::register_screen(
    UiScreenRegistration registration) {
    if (!valid_key_part(registration.owner_id) || !valid_key_part(registration.screen_id) ||
        !registration.factory) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "UiLayerStack::register_screen: owner, screen ID, and factory are required"};
    }
    if (find_record(registration.owner_id, registration.screen_id)) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidState,
            "UiLayerStack::register_screen: screen is already registered"};
    }

    const std::string owner_id = registration.owner_id;
    const std::string screen_id = registration.screen_id;
    const UiLayer layer = registration.layer;
    const bool visible = registration.initially_visible;
    screens_.push_back({.registration = std::move(registration), .visible = visible});
    SNT_LOG_INFO("MUI layer-stack screen registered: owner='%s' screen='%s' layer=%u visible=%s",
                 owner_id.c_str(), screen_id.c_str(), static_cast<unsigned>(layer),
                 visible ? "true" : "false");
    return {};
}

snt::core::Expected<void> UiLayerStack::replace_owner_screens(
    std::string_view owner_id,
    std::vector<UiScreenRegistration> registrations) {
    if (!valid_key_part(owner_id)) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "UiLayerStack::replace_owner_screens: owner ID is required"};
    }

    std::unordered_set<std::string> screen_ids;
    screen_ids.reserve(registrations.size());
    for (const UiScreenRegistration& registration : registrations) {
        if (registration.owner_id != owner_id || !valid_key_part(registration.screen_id) ||
            !registration.factory) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidArgument,
                "UiLayerStack::replace_owner_screens: registrations must use the supplied owner, "
                "a screen ID, and a factory"};
        }
        if (!screen_ids.emplace(registration.screen_id).second) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidArgument,
                "UiLayerStack::replace_owner_screens: replacement contains duplicate screen IDs"};
        }
    }

    for (const ScreenRecord& record : screens_) {
        if (record.registration.owner_id == owner_id) invalidate_interaction(record);
    }
    const size_t before = screens_.size();
    screens_.erase(std::remove_if(screens_.begin(), screens_.end(),
        [owner_id](const ScreenRecord& record) {
            return record.registration.owner_id == owner_id;
        }), screens_.end());
    const size_t after_removal = screens_.size();
    screens_.reserve(screens_.size() + registrations.size());
    for (UiScreenRegistration& registration : registrations) {
        const bool visible = registration.initially_visible;
        screens_.push_back({.registration = std::move(registration), .visible = visible});
    }

    const size_t removed = before - after_removal;
    SNT_LOG_INFO("MUI layer-stack owner replaced: owner='%.*s' removed=%zu registered=%zu",
                 static_cast<int>(owner_id.size()), owner_id.data(), removed,
                 registrations.size());
    return {};
}

snt::core::Expected<void> UiLayerStack::set_visible(std::string_view owner_id,
                                                     std::string_view screen_id,
                                                     bool visible) {
    ScreenRecord* record = find_record(owner_id, screen_id);
    if (!record) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "UiLayerStack::set_visible: screen is not registered"};
    }
    if (record->visible == visible) return {};
    if (!visible) invalidate_interaction(*record);
    record->visible = visible;
    SNT_LOG_INFO("MUI layer-stack screen visibility changed: owner='%s' screen='%s' visible=%s",
                 record->registration.owner_id.c_str(), record->registration.screen_id.c_str(),
                 visible ? "true" : "false");
    return {};
}

snt::core::Expected<void> UiLayerStack::unregister_screen(std::string_view owner_id,
                                                           std::string_view screen_id) {
    const auto found = std::find_if(screens_.begin(), screens_.end(),
        [owner_id, screen_id](const ScreenRecord& record) {
            return record.registration.owner_id == owner_id &&
                   record.registration.screen_id == screen_id;
        });
    if (found == screens_.end()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "UiLayerStack::unregister_screen: screen is not registered"};
    }
    invalidate_interaction(*found);
    SNT_LOG_INFO("MUI layer-stack screen unregistered: owner='%s' screen='%s'",
                 found->registration.owner_id.c_str(), found->registration.screen_id.c_str());
    screens_.erase(found);
    return {};
}

size_t UiLayerStack::unregister_owner(std::string_view owner_id) {
    const size_t before = screens_.size();
    for (const ScreenRecord& record : screens_) {
        if (record.registration.owner_id == owner_id) invalidate_interaction(record);
    }
    screens_.erase(std::remove_if(screens_.begin(), screens_.end(),
        [owner_id](const ScreenRecord& record) {
            return record.registration.owner_id == owner_id;
        }), screens_.end());
    const size_t removed = before - screens_.size();
    if (removed > 0) {
        SNT_LOG_INFO("MUI layer-stack owner unregistered: owner='%.*s' screens=%zu",
                     static_cast<int>(owner_id.size()), owner_id.data(), removed);
    }
    return removed;
}

bool UiLayerStack::is_registered(std::string_view owner_id,
                                 std::string_view screen_id) const {
    return find_record(owner_id, screen_id) != nullptr;
}

bool UiLayerStack::is_visible(std::string_view owner_id,
                              std::string_view screen_id) const {
    const ScreenRecord* record = find_record(owner_id, screen_id);
    return record && record->visible;
}

bool UiLayerStack::is_mounted(std::string_view owner_id,
                              std::string_view screen_id) const {
    const ScreenRecord* record = find_record(owner_id, screen_id);
    return record && record->mounted.root != nullptr;
}

const std::vector<UiScreenSubmission>& UiLayerStack::prepare_frame(
    const UiScreenFrameContext& context) {
    frame_submissions_.clear();
    frame_submissions_.reserve(screens_.size());

    for (ScreenRecord& record : screens_) {
        if (!record.visible) continue;

        if (!record.mounted.root) {
            auto mounted = record.registration.factory({
                .viewport = context.viewport,
                .ui_viewport = context.ui_viewport,
                .images = context.images,
                .dispatch_action = record.registration.dispatch_action,
            });
            if (!mounted || !mounted->root) {
                if (!record.mount_failure_logged) {
                    record.mount_failure_logged = true;
                    if (!mounted) {
                        SNT_LOG_ERROR("MUI screen mount failed: owner='%s' screen='%s' error=%s",
                                      record.registration.owner_id.c_str(),
                                      record.registration.screen_id.c_str(),
                                      mounted.error().format().c_str());
                    } else {
                        SNT_LOG_ERROR("MUI screen mount returned a null root: owner='%s' screen='%s'",
                                      record.registration.owner_id.c_str(),
                                      record.registration.screen_id.c_str());
                    }
                }
                continue;
            }

            mounted->root->set_id(qualified_id(record.registration.owner_id,
                                                record.registration.screen_id));
            record.mounted = std::move(*mounted);
            record.mount_failure_logged = false;
            SNT_LOG_INFO("MUI screen mounted: owner='%s' screen='%s' layer=%u",
                         record.registration.owner_id.c_str(),
                         record.registration.screen_id.c_str(),
                         static_cast<unsigned>(record.registration.layer));
        }

        if (record.mounted.update) {
            record.mounted.update(*record.mounted.root, context);
        }
        frame_submissions_.push_back({
            .layer = record.registration.layer,
            .input_policy = ui_layer_input_policy(record.registration.layer),
            .root = record.mounted.root.get(),
        });
    }
    return frame_submissions_;
}

void UiLayerStack::invalidate_interaction(const ScreenRecord& record) {
    if (!record.mounted.root || record.mounted.root->id().empty()) return;
    if (retained_root_invalidator_) retained_root_invalidator_(*record.mounted.root);
}


}  // namespace snt::ui