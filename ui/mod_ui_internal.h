// Internal bridge from the value-only mod facade to retained MUI.

#pragma once

#include "ui/mod_ui.h"

#include <memory>

namespace snt::ui {

class UiImageRegistry;
class UiLayerStack;

namespace mod::internal {

// This factory is intentionally internal. A mod receives only IModUiHost;
// engine code owns the retained layer stack, image registry, and host lifetime.
[[nodiscard]] snt::core::Expected<std::unique_ptr<IModUiHost>> create_mod_ui_host(
    OwnerId owner,
    UiLayerStack& layers,
    UiImageRegistry& images,
    IModUiCommandSink& command_sink);

}  // namespace mod::internal
}  // namespace snt::ui
