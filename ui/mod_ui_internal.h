// Internal bridge from the value-only mod facade to retained MUI.

#pragma once

#include "ui/mod_ui.h"

#include <memory>

namespace snt::ui {

class UiImageRegistry;
class UiLayerStack;

namespace mod::internal {

// This factory is intentionally internal. A Mod receives only IModUiHost;
// the client runtime owns the retained layer stack, image registry, and this
// gateway's teardown order.
[[nodiscard]] snt::core::Expected<std::unique_ptr<IModUiRuntime>> create_mod_ui_runtime(
    UiLayerStack& layers,
    UiImageRegistry& images);

}  // namespace mod::internal
}  // namespace snt::ui
