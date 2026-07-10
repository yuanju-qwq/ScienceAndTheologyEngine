// RegistryHub -- implementation.

#define SNT_LOG_CHANNEL "script"

#include "script/registry_hub.h"

#include <algorithm>
#include <utility>

#include "core/error.h"
#include "core/log.h"

namespace snt::script {
namespace {

snt::core::Expected<void> invalid_argument(std::string message) {
    return snt::core::Error{snt::core::ErrorCode::kInvalidArgument, message};
}

snt::core::Expected<void> invalid_state(std::string message) {
    return snt::core::Error{snt::core::ErrorCode::kInvalidState, message};
}

}  // namespace

snt::core::Expected<void> RegistryHub::validate(const RecipeDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Recipe id must not be empty");
    if (definition.machine_id.empty()) return invalid_argument("Recipe machine_id must not be empty");
    if (definition.input_item_id.empty()) return invalid_argument("Recipe input_item_id must not be empty");
    if (definition.outputs.empty()) return invalid_argument("Recipe must have at least one output");
    if (definition.duration_ticks <= 0) return invalid_argument("Recipe duration_ticks must be positive");
    if (definition.energy_per_tick < 0) return invalid_argument("Recipe energy_per_tick must not be negative");
    for (const auto& output : definition.outputs) {
        if (output.item_id.empty() || output.count <= 0) {
            return invalid_argument("Recipe output item_id must not be empty and count must be positive");
        }
    }
    return {};
}

snt::core::Expected<void> RegistryHub::validate(const MachineDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Machine id must not be empty");
    if (definition.display_name.empty()) return invalid_argument("Machine display_name must not be empty");
    if (definition.tier <= 0) return invalid_argument("Machine tier must be positive");
    if (definition.power_capacity < 0) return invalid_argument("Machine power_capacity must not be negative");
    return {};
}

snt::core::Expected<void> RegistryHub::validate(const QuestDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Quest id must not be empty");
    if (definition.title.empty()) return invalid_argument("Quest title must not be empty");
    return {};
}

snt::core::Expected<void> RegistryHub::validate(const EventListener& listener) {
    if (listener.script_id == kBuiltinScriptId) {
        return invalid_argument("Event listener must have a non-builtin ScriptId");
    }
    if (listener.event_name.empty()) return invalid_argument("Event name must not be empty");
    if (listener.callback_id.empty()) return invalid_argument("Event callback_id must not be empty");
    return {};
}

snt::core::Expected<void> RegistryHub::register_builtin_recipe(RecipeDefinition definition) {
    return register_recipe(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> RegistryHub::register_builtin_machine(MachineDefinition definition) {
    return register_machine(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> RegistryHub::register_builtin_quest(QuestDefinition definition) {
    return register_quest(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> RegistryHub::register_script_recipe(ScriptId script_id,
                                                               RecipeDefinition definition) {
    return register_recipe(script_id, std::move(definition), false);
}

snt::core::Expected<void> RegistryHub::register_script_machine(ScriptId script_id,
                                                                MachineDefinition definition) {
    return register_machine(script_id, std::move(definition), false);
}

snt::core::Expected<void> RegistryHub::register_script_quest(ScriptId script_id,
                                                              QuestDefinition definition) {
    return register_quest(script_id, std::move(definition), false);
}

snt::core::Expected<void> RegistryHub::register_recipe(ScriptId owner,
                                                        RecipeDefinition definition,
                                                        bool builtin) {
    auto valid = validate(definition);
    if (!valid) return valid.error();
    if (builtin != (owner == kBuiltinScriptId)) {
        return invalid_argument("Built-in registrations must use ScriptId 0");
    }

    const std::string id = definition.id;
    auto existing = live_recipes_.find(id);
    if (!builtin && existing != live_recipes_.end() &&
        existing->second.owner != kBuiltinScriptId && existing->second.owner != owner) {
        return invalid_state("Recipe id is already owned by another script: " + id);
    }
    if (builtin && existing != live_recipes_.end() &&
        existing->second.owner != kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script recipe with a built-in recipe: " + id);
    }

    OwnedDefinition<RecipeDefinition> entry{owner, std::move(definition)};
    if (builtin) backup_recipes_[id] = entry;
    live_recipes_[id] = std::move(entry);
    return {};
}

snt::core::Expected<void> RegistryHub::register_machine(ScriptId owner,
                                                         MachineDefinition definition,
                                                         bool builtin) {
    auto valid = validate(definition);
    if (!valid) return valid.error();
    if (builtin != (owner == kBuiltinScriptId)) {
        return invalid_argument("Built-in registrations must use ScriptId 0");
    }

    const std::string id = definition.id;
    auto existing = live_machines_.find(id);
    if (!builtin && existing != live_machines_.end() &&
        existing->second.owner != kBuiltinScriptId && existing->second.owner != owner) {
        return invalid_state("Machine id is already owned by another script: " + id);
    }
    if (builtin && existing != live_machines_.end() &&
        existing->second.owner != kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script machine with a built-in machine: " + id);
    }

    OwnedDefinition<MachineDefinition> entry{owner, std::move(definition)};
    if (builtin) backup_machines_[id] = entry;
    live_machines_[id] = std::move(entry);
    return {};
}

snt::core::Expected<void> RegistryHub::register_quest(ScriptId owner,
                                                       QuestDefinition definition,
                                                       bool builtin) {
    auto valid = validate(definition);
    if (!valid) return valid.error();
    if (builtin != (owner == kBuiltinScriptId)) {
        return invalid_argument("Built-in registrations must use ScriptId 0");
    }

    const std::string id = definition.id;
    auto existing = live_quests_.find(id);
    if (!builtin && existing != live_quests_.end() &&
        existing->second.owner != kBuiltinScriptId && existing->second.owner != owner) {
        return invalid_state("Quest id is already owned by another script: " + id);
    }
    if (builtin && existing != live_quests_.end() &&
        existing->second.owner != kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script quest with a built-in quest: " + id);
    }

    OwnedDefinition<QuestDefinition> entry{owner, std::move(definition)};
    if (builtin) backup_quests_[id] = entry;
    live_quests_[id] = std::move(entry);
    return {};
}

const RecipeDefinition* RegistryHub::find_recipe(std::string_view id) const {
    auto it = live_recipes_.find(id);
    return it == live_recipes_.end() ? nullptr : &it->second.definition;
}

const MachineDefinition* RegistryHub::find_machine(std::string_view id) const {
    auto it = live_machines_.find(id);
    return it == live_machines_.end() ? nullptr : &it->second.definition;
}

const QuestDefinition* RegistryHub::find_quest(std::string_view id) const {
    auto it = live_quests_.find(id);
    return it == live_quests_.end() ? nullptr : &it->second.definition;
}

std::vector<RecipeDefinition> RegistryHub::recipes_for_machine(std::string_view machine_id) const {
    std::vector<RecipeDefinition> result;
    for (const auto& [id, entry] : live_recipes_) {
        (void)id;
        if (entry.definition.machine_id == machine_id) result.push_back(entry.definition);
    }
    return result;
}

snt::core::Expected<void> RegistryHub::add_event_listener(EventListener listener) {
    auto valid = validate(listener);
    if (!valid) return valid.error();

    auto& listeners = event_listeners_[listener.event_name];
    const auto duplicate = std::find(listeners.begin(), listeners.end(), listener);
    if (duplicate != listeners.end()) {
        return invalid_state("Duplicate event listener: " + listener.event_name + "/" + listener.callback_id);
    }
    const std::string event_name = listener.event_name;
    listeners.push_back(std::move(listener));
    sort_event_listeners(event_name);
    return {};
}

std::vector<EventListener> RegistryHub::event_listeners(std::string_view event_name) const {
    auto it = event_listeners_.find(event_name);
    return it == event_listeners_.end() ? std::vector<EventListener>{} : it->second;
}

snt::core::Expected<void> RegistryHub::set_state(ScriptId script_id,
                                                  std::string key,
                                                  std::string value) {
    if (script_id == kBuiltinScriptId) return invalid_argument("StateStore requires a non-builtin ScriptId");
    if (key.empty()) return invalid_argument("StateStore key must not be empty");
    state_store_[script_id][std::move(key)] = std::move(value);
    return {};
}

std::optional<std::string> RegistryHub::get_state(ScriptId script_id,
                                                   std::string_view key) const {
    auto script = state_store_.find(script_id);
    if (script == state_store_.end()) return std::nullopt;
    auto value = script->second.find(key);
    if (value == script->second.end()) return std::nullopt;
    return value->second;
}

snt::core::Expected<void> RegistryHub::begin_reload(ScriptId script_id) {
    if (script_id == kBuiltinScriptId) return invalid_argument("Built-in content cannot be reloaded as a script");
    if (reloads_.contains(script_id)) return invalid_state("Reload is already active for script");

    reloads_.emplace(script_id, snapshot_script_content(script_id));
    erase_script_content(script_id);
    SNT_LOG_DEBUG("RegistryHub began transactional reload for script %llu",
                  static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> RegistryHub::commit_reload(ScriptId script_id) {
    auto it = reloads_.find(script_id);
    if (it == reloads_.end()) return invalid_state("No active reload for script");
    reloads_.erase(it);
    SNT_LOG_INFO("RegistryHub committed reload for script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> RegistryHub::rollback_reload(ScriptId script_id) {
    auto it = reloads_.find(script_id);
    if (it == reloads_.end()) return invalid_state("No active reload for script");

    erase_script_content(script_id);
    restore_script_content(it->second);
    reloads_.erase(it);
    SNT_LOG_WARN("RegistryHub rolled back reload for script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> RegistryHub::unload_script(ScriptId script_id) {
    if (script_id == kBuiltinScriptId) return invalid_argument("Built-in content cannot be unloaded as a script");
    if (reloads_.contains(script_id)) return invalid_state("Cannot unload a script during its active reload");
    erase_script_content(script_id);
    SNT_LOG_INFO("RegistryHub unloaded script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

void RegistryHub::reset() {
    backup_recipes_.clear();
    backup_machines_.clear();
    backup_quests_.clear();
    live_recipes_.clear();
    live_machines_.clear();
    live_quests_.clear();
    event_listeners_.clear();
    state_store_.clear();
    reloads_.clear();
}

RegistryHub::ReloadSnapshot RegistryHub::snapshot_script_content(ScriptId script_id) const {
    ReloadSnapshot snapshot;
    for (const auto& [id, entry] : live_recipes_) {
        if (entry.owner == script_id) snapshot.recipes.emplace(id, entry);
    }
    for (const auto& [id, entry] : live_machines_) {
        if (entry.owner == script_id) snapshot.machines.emplace(id, entry);
    }
    for (const auto& [id, entry] : live_quests_) {
        if (entry.owner == script_id) snapshot.quests.emplace(id, entry);
    }
    for (const auto& [event_name, listeners] : event_listeners_) {
        (void)event_name;
        for (const auto& listener : listeners) {
            if (listener.script_id == script_id) snapshot.event_listeners.push_back(listener);
        }
    }
    return snapshot;
}

void RegistryHub::erase_script_content(ScriptId script_id) {
    for (auto it = live_recipes_.begin(); it != live_recipes_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        auto backup = backup_recipes_.find(it->first);
        if (backup == backup_recipes_.end()) it = live_recipes_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    for (auto it = live_machines_.begin(); it != live_machines_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        auto backup = backup_machines_.find(it->first);
        if (backup == backup_machines_.end()) it = live_machines_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    for (auto it = live_quests_.begin(); it != live_quests_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        auto backup = backup_quests_.find(it->first);
        if (backup == backup_quests_.end()) it = live_quests_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    for (auto events = event_listeners_.begin(); events != event_listeners_.end();) {
        auto& listeners = events->second;
        std::erase_if(listeners, [script_id](const EventListener& listener) {
            return listener.script_id == script_id;
        });
        if (listeners.empty()) events = event_listeners_.erase(events);
        else ++events;
    }
}

void RegistryHub::restore_script_content(const ReloadSnapshot& snapshot) {
    for (const auto& [id, entry] : snapshot.recipes) live_recipes_[id] = entry;
    for (const auto& [id, entry] : snapshot.machines) live_machines_[id] = entry;
    for (const auto& [id, entry] : snapshot.quests) live_quests_[id] = entry;
    for (const auto& listener : snapshot.event_listeners) {
        event_listeners_[listener.event_name].push_back(listener);
        sort_event_listeners(listener.event_name);
    }
}

void RegistryHub::sort_event_listeners(std::string_view event_name) {
    auto it = event_listeners_.find(event_name);
    if (it == event_listeners_.end()) return;
    std::sort(it->second.begin(), it->second.end(),
              [](const EventListener& lhs, const EventListener& rhs) {
                  if (lhs.script_id != rhs.script_id) return lhs.script_id < rhs.script_id;
                  return lhs.callback_id < rhs.callback_id;
              });
}

}  // namespace snt::script
