// Script manager — implementation.

#define SNT_LOG_CHANNEL "script"

#include "script/script_manager.h"

#include "core/log.h"
#include "core/error.h"

namespace snt::script {

ScriptManager& ScriptManager::instance() {
    static ScriptManager inst;
    return inst;
}

snt::core::Expected<void> ScriptManager::init() {
    SNT_LOG_INFO("Initializing ScriptManager...");

    auto r = engine_.init();
    if (!r) {
        return r.error().with_context("ScriptManager: engine_.init");
    }

    // Register core types (string + primitives) so basic scripts compile.
    r = engine_.register_core_types();
    if (!r) {
        return r.error().with_context("ScriptManager: register_core_types");
    }

    // Pre-allocate one context; more are created on demand.
    r = contexts_.init(engine_.raw(), 1);
    if (!r) {
        return r.error().with_context("ScriptManager: contexts_.init");
    }

    loader_.set_engine(engine_.raw());

    SNT_LOG_INFO("ScriptManager initialized");
    return {};
}

void ScriptManager::update(float /*dt*/) {
    // Poll for .as file changes and reload if any file was touched.
    size_t n = loader_.reload_if_changed(contexts_);
    if (n > 0) {
        SNT_LOG_INFO("Reloaded %zu script module(s)", n);
    }

    // One incremental GC step per frame keeps script-allocated objects
    // (strings, arrays) from accumulating without long pauses.
    contexts_.gc_step();
}

void ScriptManager::shutdown() {
    SNT_LOG_INFO("Shutting down ScriptManager...");
    loader_.unload_all();
    contexts_.shutdown();
    engine_.shutdown();
    SNT_LOG_INFO("ScriptManager shut down");
}

}  // namespace snt::script
