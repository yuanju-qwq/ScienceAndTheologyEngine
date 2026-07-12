// Script content-host contract.
//
// Ownership: ScriptManager owns the generic AngelScript VM, loader and file
// watcher. The game supplies one IScriptContentHost for its content model.
// Lifecycle: configure the host before ScriptManager::init(); it remains
// borrowed until shutdown, when the manager resets it and releases the
// reference. Thread affinity: every method is called on the main thread.
//
// The contract deliberately contains no recipe, quest, item, or world types.
// It gives the engine only the transaction operations needed to preserve a
// loaded module across content-registration failures and hot reloads.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/expected.h"

class asIScriptEngine;

namespace snt::script {

using ScriptId = uint64_t;
constexpr ScriptId kBuiltinScriptId = 0;

// An opaque main-thread scope that makes one ScriptId active while its
// `snt_register()` entry point executes. Content hosts use it to keep native
// registration functions from observing stale reload context.
class IScriptRegistrationScope {
public:
    virtual ~IScriptRegistrationScope() = default;
};

class IScriptContentHost {
public:
    virtual ~IScriptContentHost() = default;

    // Register this host's script declarations after core AngelScript types
    // are available and before any script module is compiled.
    virtual snt::core::Expected<void> register_script_api(asIScriptEngine* engine) = 0;

    // The returned scope must remain alive while the loader calls the script
    // registration entry point. A null scope is treated as a host error.
    virtual std::unique_ptr<IScriptRegistrationScope> begin_registration(
        ScriptId script_id) = 0;

    // Transaction lifecycle for one stable ScriptId. begin_reload removes
    // live registrations; commit keeps the replacement; rollback restores
    // the prior content after compilation, registration, or validation fails.
    virtual snt::core::Expected<void> begin_reload(ScriptId script_id) = 0;
    virtual snt::core::Expected<void> commit_reload(ScriptId script_id) = 0;
    virtual snt::core::Expected<void> rollback_reload(ScriptId script_id) = 0;
    virtual snt::core::Expected<void> unload_script(ScriptId script_id) = 0;

    // Return callback names registered by this script in deterministic order.
    // The loader verifies each name against the candidate module before commit.
    virtual std::vector<std::string> callback_ids_for_script(ScriptId script_id) const = 0;

    // Clear all host-owned live content and transient script state at the
    // ScriptManager session boundary.
    virtual void reset() = 0;
};

}  // namespace snt::script
