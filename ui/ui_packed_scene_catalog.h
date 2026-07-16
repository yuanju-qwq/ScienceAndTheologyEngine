// UiPackedScene owner catalog for package and Mod lifecycle.
//
// Package loaders use this boundary to turn validated resource files into
// namespaced UiLayerStack registrations. It deliberately knows nothing
// about a specific scripting runtime or archive format: callers supply the
// action dispatcher and choose whether scenes arrive from files or memory.

#pragma once

#include "ui_packed_scene.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace snt::ui {

struct UiPackedSceneScreenRegistration {
    std::string screen_id;
    std::shared_ptr<const UiPackedScene> scene;
    UiLayer layer = UiLayer::Screen;
    bool initially_visible = false;
    UiActionDispatcher dispatch_action;
};

struct UiPackedSceneFileScreenRegistration {
    std::string screen_id;
    std::filesystem::path source_path;
    UiLayer layer = UiLayer::Screen;
    bool initially_visible = false;
    UiActionDispatcher dispatch_action;
};

// Atomically switches the complete UI contribution for one package owner.
// Every scene is validated before UiLayerStack mutates, so callers can
// retain a currently working UI when a Mod resource reload is invalid.
[[nodiscard]] snt::core::Expected<void> replace_ui_packed_scene_owner(
    UiLayerStack& layers,
    std::string owner_id,
    std::vector<UiPackedSceneScreenRegistration> registrations);

// File-backed convenience for editor exports and unpacked Mod directories.
// Archive/network packages can parse assets themselves and call the memory
// overload above, preserving the same lifecycle semantics.
[[nodiscard]] snt::core::Expected<void> load_ui_packed_scene_owner(
    UiLayerStack& layers,
    std::string owner_id,
    std::vector<UiPackedSceneFileScreenRegistration> registrations);

}  // namespace snt::ui
