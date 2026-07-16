// UiPackedScene owner catalog implementation.

#define SNT_LOG_CHANNEL "ui.packed_scene_catalog"
#include "ui_packed_scene_catalog.h"

#include "core/error.h"
#include "core/log.h"

#include <utility>

namespace snt::ui {
namespace {

snt::core::Error invalid_argument(std::string message) {
    return snt::core::Error{snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

}  // namespace

snt::core::Expected<void> replace_ui_packed_scene_owner(
    UiLayerStack& layers,
    std::string owner_id,
    std::vector<UiPackedSceneScreenRegistration> registrations) {
    if (owner_id.empty()) {
        return invalid_argument("UiPackedScene owner ID must not be empty");
    }

    std::vector<UiScreenRegistration> screen_registrations;
    screen_registrations.reserve(registrations.size());
    for (UiPackedSceneScreenRegistration& registration : registrations) {
        if (!registration.scene) {
            return invalid_argument("UiPackedScene registration requires a scene");
        }
        if (auto valid = validate_ui_packed_scene(*registration.scene); !valid) {
            auto error = valid.error();
            error.with_context("UiPackedScene registration '" + registration.screen_id + "'");
            return error;
        }
        screen_registrations.push_back({
            .owner_id = owner_id,
            .screen_id = std::move(registration.screen_id),
            .layer = registration.layer,
            .initially_visible = registration.initially_visible,
            .factory = make_ui_packed_scene_factory(std::move(registration.scene)),
            .dispatch_action = std::move(registration.dispatch_action),
        });
    }

    if (auto result = layers.replace_owner_screens(owner_id, std::move(screen_registrations));
        !result) {
        auto error = result.error();
        error.with_context("replace_ui_packed_scene_owner(" + owner_id + ")");
        return error;
    }
    SNT_LOG_INFO("Loaded packed UI owner '%s' with %zu screen(s)",
                 owner_id.c_str(), registrations.size());
    return {};
}

snt::core::Expected<void> load_ui_packed_scene_owner(
    UiLayerStack& layers,
    std::string owner_id,
    std::vector<UiPackedSceneFileScreenRegistration> registrations) {
    std::vector<UiPackedSceneScreenRegistration> loaded;
    loaded.reserve(registrations.size());
    for (UiPackedSceneFileScreenRegistration& registration : registrations) {
        auto scene = load_ui_packed_scene_file(registration.source_path);
        if (!scene) {
            auto error = scene.error();
            error.with_context("load_ui_packed_scene_owner(" + owner_id + ", " +
                               registration.source_path.string() + ")");
            return error;
        }
        loaded.push_back({
            .screen_id = std::move(registration.screen_id),
            .scene = std::make_shared<const UiPackedScene>(std::move(*scene)),
            .layer = registration.layer,
            .initially_visible = registration.initially_visible,
            .dispatch_action = std::move(registration.dispatch_action),
        });
    }
    return replace_ui_packed_scene_owner(layers, std::move(owner_id), std::move(loaded));
}

}  // namespace snt::ui
