// Test-only generic script content host.
//
// Engine tests use this host to exercise ScriptManager and ScriptLoader
// lifecycle behavior without importing a game's definitions or bindings.

#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/error.h"
#include "script/content_host.h"

namespace snt::test {

class EmptyScriptRegistrationScope final : public snt::script::IScriptRegistrationScope {};

class TestScriptContentHost final : public snt::script::IScriptContentHost {
public:
    snt::core::Expected<void> register_script_api(asIScriptEngine*) override { return {}; }

    std::unique_ptr<snt::script::IScriptRegistrationScope> begin_registration(
        snt::script::ScriptId) override {
        return std::make_unique<EmptyScriptRegistrationScope>();
    }

    snt::core::Expected<void> begin_reload(snt::script::ScriptId script_id) override {
        if (script_id == snt::script::kBuiltinScriptId || !active_reloads_.insert(script_id).second) {
            return invalid_state("Invalid test content-host reload begin");
        }
        return {};
    }

    snt::core::Expected<void> commit_reload(snt::script::ScriptId script_id) override {
        return finish_reload(script_id);
    }

    snt::core::Expected<void> rollback_reload(snt::script::ScriptId script_id) override {
        return finish_reload(script_id);
    }

    snt::core::Expected<void> unload_script(snt::script::ScriptId script_id) override {
        if (active_reloads_.contains(script_id)) {
            return invalid_state("Cannot unload a test script during reload");
        }
        return {};
    }

    std::vector<std::string> callback_ids_for_script(snt::script::ScriptId) const override {
        return {};
    }

    void reset() override { active_reloads_.clear(); }

private:
    static snt::core::Expected<void> invalid_state(const char* message) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState, message};
    }

    snt::core::Expected<void> finish_reload(snt::script::ScriptId script_id) {
        if (active_reloads_.erase(script_id) == 0) {
            return invalid_state("No active test content-host reload");
        }
        return {};
    }

    std::set<snt::script::ScriptId> active_reloads_;
};

}  // namespace snt::test
