// Script manager: top-level façade owning ScriptEngine, ScriptContextPool,
// and ScriptLoader.
//
// Lifecycle:
//   - init()    — create AS engine + context pool + register core types
//   - update()  — poll for .as file changes; run one GC step
//   - shutdown() — release everything in reverse order
//
// ScriptManager is a global singleton (instance()), matching the style
// of AssetManager. Gameplay code calls ScriptManager::instance().load_*
// to register scripts, and the Engine calls update() each frame.

#pragma once

#include <string>
#include <string_view>

#include "core/expected.h"
#include "script/script_engine.h"
#include "script/script_context.h"
#include "script/script_loader.h"
#include "script/registry_hub.h"

namespace snt::script {

class ScriptManager {
public:
    // Singleton accessor. Not thread-safe on first construction.
    static ScriptManager& instance();

    // Create the AS engine, context pool, and register core types.
    snt::core::Expected<void> init();

    // Per-frame update: check for .as file changes and reload if needed.
    // Also runs one incremental GC step to keep script memory bounded.
    // `dt` is the frame delta in seconds (currently unused, reserved
    // for future throttling of reload checks).
    void update(float dt);

    // Release the AS engine and all modules.
    void shutdown();

    // ---- Script API (used by Engine / gameplay code) ----

    // Load all .as files under `dir` recursively.
    snt::core::Expected<void> load_directory(std::string_view dir) {
        return loader_.load_directory(dir);
    }

    // Load a single .as file.
    snt::core::Expected<void> load_file(std::string_view path) {
        return loader_.load_file(path);
    }

    // Load an inline source string (no file backing).
    snt::core::Expected<void> load_source(std::string_view name,
                                          std::string_view source) {
        return loader_.load_source(name, source);
    }

    // Look up a loaded module by name.
    ScriptModule* get_module(std::string_view name) {
        return loader_.get_module(name);
    }

    ScriptEngine&       engine()  { return engine_; }
    ScriptContextPool&  contexts() { return contexts_; }
    ScriptLoader&       loader()  { return loader_; }
    RegistryHub&        registries() { return registry_hub_; }
    const RegistryHub&  registries() const { return registry_hub_; }

private:
    ScriptManager() = default;
    ~ScriptManager() = default;

    ScriptManager(const ScriptManager&) = delete;
    ScriptManager& operator=(const ScriptManager&) = delete;

    ScriptEngine      engine_;
    ScriptContextPool contexts_;
    ScriptLoader      loader_;
    RegistryHub       registry_hub_;
};

}  // namespace snt::script
