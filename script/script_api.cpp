// P7.1 gameplay Script API implementation.

#define SNT_LOG_CHANNEL "script"
#include "script/script_api.h"

#include <angelscript.h>

#include <string>

#include "core/error.h"
#include "core/log.h"

namespace snt::script {

namespace {

thread_local RegistryHub* g_registries = nullptr;
thread_local ScriptId g_script_id = kBuiltinScriptId;

void report_binding_error(const snt::core::Error& error) {
    SNT_LOG_ERROR("Script API rejected registration: %s", error.format().c_str());
    if (asIScriptContext* context = asGetActiveContext()) {
        context->SetException(error.format().c_str());
    }
}

RegistryHub* active_registries() {
    if (g_registries && g_script_id != kBuiltinScriptId) {
        return g_registries;
    }
    report_binding_error(snt::core::Error{
        snt::core::ErrorCode::kInvalidState,
        "Gameplay registration was called outside snt_register()"});
    return nullptr;
}

void api_register_recipe(const std::string& id,
                         const std::string& machine_id,
                         const std::string& input_item_id,
                         const std::string& output_item_id,
                         int output_count,
                         int duration_ticks,
                         int energy_per_tick,
                         const std::string& tag) {
    RegistryHub* registries = active_registries();
    if (!registries) return;

    RecipeDefinition definition;
    definition.id = id;
    definition.machine_id = machine_id;
    definition.input_item_id = input_item_id;
    definition.outputs = {RecipeOutputDefinition{output_item_id, output_count}};
    definition.duration_ticks = duration_ticks;
    definition.energy_per_tick = energy_per_tick;
    definition.tag = tag;
    if (auto result = registries->register_script_recipe(g_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_register_machine(const std::string& id,
                          const std::string& display_name,
                          int tier,
                          int power_capacity) {
    RegistryHub* registries = active_registries();
    if (!registries) return;

    MachineDefinition definition;
    definition.id = id;
    definition.display_name = display_name;
    definition.tier = tier;
    definition.power_capacity = power_capacity;
    if (auto result = registries->register_script_machine(g_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_register_quest(const std::string& id,
                        const std::string& title,
                        const std::string& description) {
    RegistryHub* registries = active_registries();
    if (!registries) return;

    QuestDefinition definition;
    definition.id = id;
    definition.title = title;
    definition.description = description;
    if (auto result = registries->register_script_quest(g_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_on(const std::string& event_name, const std::string& callback_id) {
    RegistryHub* registries = active_registries();
    if (!registries) return;

    if (auto result = registries->add_event_listener(
            EventListener{g_script_id, event_name, callback_id}); !result) {
        report_binding_error(result.error());
    }
}

void api_set_state(const std::string& key, const std::string& value) {
    RegistryHub* registries = active_registries();
    if (!registries) return;

    if (auto result = registries->set_state(g_script_id, key, value); !result) {
        report_binding_error(result.error());
    }
}

std::string api_get_state(const std::string& key) {
    RegistryHub* registries = active_registries();
    if (!registries) return {};
    return registries->get_state(g_script_id, key).value_or("");
}

void api_log(const std::string& message) {
    SNT_LOG_INFO("[AS] %s", message.c_str());
}

snt::core::Expected<void> register_function(asIScriptEngine* engine,
                                             const char* declaration,
                                             const asSFuncPtr& function) {
    const int result = engine->RegisterGlobalFunction(declaration, function, asCALL_CDECL);
    if (result >= 0) return {};
    return snt::core::Error{
        snt::core::ErrorCode::kScriptEngineInitFailed,
        std::string("RegisterGlobalFunction failed for '") + declaration + "': " +
            std::to_string(result)};
}

}  // namespace

snt::core::Expected<void> register_gameplay_script_api(asIScriptEngine* engine) {
    if (!engine) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "register_gameplay_script_api received null engine"};
    }

    if (auto result = register_function(
            engine,
            "void snt_register_recipe(const string &in, const string &in, const string &in, const string &in, int, int, int, const string &in)",
            asFUNCTION(api_register_recipe)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_machine(const string &in, const string &in, int, int)",
            asFUNCTION(api_register_machine)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_quest(const string &in, const string &in, const string &in)",
            asFUNCTION(api_register_quest)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_on(const string &in, const string &in)",
            asFUNCTION(api_on)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_set_state(const string &in, const string &in)",
            asFUNCTION(api_set_state)); !result) return result;
    if (auto result = register_function(
            engine, "string snt_get_state(const string &in)",
            asFUNCTION(api_get_state)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_log(const string &in)", asFUNCTION(api_log)); !result) return result;

    SNT_LOG_INFO("Registered P7 gameplay Script API");
    return {};
}

ScriptRegistrationScope::ScriptRegistrationScope(RegistryHub& registries, ScriptId script_id)
    : previous_registries_(g_registries)
    , previous_script_id_(g_script_id) {
    g_registries = &registries;
    g_script_id = script_id;
}

ScriptRegistrationScope::~ScriptRegistrationScope() {
    g_registries = previous_registries_;
    g_script_id = previous_script_id_;
}

}  // namespace snt::script
