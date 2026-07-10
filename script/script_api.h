// P7.1 gameplay Script API -- controlled AngelScript registration boundary.
//
// Scripts can only register copied value definitions and stable callback
// identifiers through this layer. They never receive RegistryHub ownership,
// native pointers, or AngelScript function pointers that could outlive a
// hot-reloaded module.

#pragma once

#include "core/expected.h"
#include "script/registry_hub.h"

class asIScriptEngine;

namespace snt::script {

// Register the global `snt_*` declarations available to gameplay `.as`
// modules. Call once after ScriptEngine has registered core string support.
snt::core::Expected<void> register_gameplay_script_api(asIScriptEngine* engine);

// Activates one script's RegistryHub ownership while its `snt_register()`
// entry point executes. The scope is main-thread-only and restores a nested
// caller's state on destruction so failed reloads cannot retain stale binding
// context.
class ScriptRegistrationScope {
public:
    ScriptRegistrationScope(RegistryHub& registries, ScriptId script_id);
    ~ScriptRegistrationScope();

    ScriptRegistrationScope(const ScriptRegistrationScope&) = delete;
    ScriptRegistrationScope& operator=(const ScriptRegistrationScope&) = delete;

private:
    RegistryHub* previous_registries_ = nullptr;
    ScriptId previous_script_id_ = kBuiltinScriptId;
};

}  // namespace snt::script
