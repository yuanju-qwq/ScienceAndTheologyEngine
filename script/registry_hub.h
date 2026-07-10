// RegistryHub -- P7 gameplay-content ownership and reload boundary.
//
// This module owns the engine-native definitions registered by AngelScript:
// recipes, machines, quests, event listeners, and script-persistent state.
// Every mutable registration has a ScriptId owner. A script reload is a
// transaction: begin_reload() temporarily removes its entries, and either
// commit_reload() keeps newly registered content or rollback_reload() restores
// the previous content after a compile/entry-point failure.
//
// All methods are main-thread only. Script callbacks are represented by stable
// (ScriptId, callback_id) data, never by AngelScript function pointers, so no
// stale VM pointers survive a module rebuild.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/expected.h"

namespace snt::script {

using ScriptId = uint64_t;
constexpr ScriptId kBuiltinScriptId = 0;

struct RecipeOutputDefinition {
    std::string item_id;
    int32_t count = 1;
};

struct RecipeDefinition {
    std::string id;
    std::string machine_id;
    std::string input_item_id;
    std::vector<RecipeOutputDefinition> outputs;
    int32_t duration_ticks = 200;
    int32_t energy_per_tick = 0;
    std::string tag;
};

struct MachineDefinition {
    std::string id;
    std::string display_name;
    int32_t tier = 1;
    int32_t power_capacity = 0;
    std::vector<std::string> recipe_types;
};

struct QuestDefinition {
    std::string id;
    std::string title;
    std::string description;
    std::vector<std::string> prerequisites;
    std::vector<std::string> objectives;
    std::vector<std::string> rewards;
};

struct EventListener {
    ScriptId script_id = kBuiltinScriptId;
    std::string event_name;
    std::string callback_id;

    friend bool operator==(const EventListener&, const EventListener&) = default;
};

// Content registry shared by ScriptManager and future C++ gameplay systems.
// Definitions are copied on registration so script VM memory is never retained
// by gameplay systems. Returned definition pointers are valid until the next
// mutating RegistryHub call.
class RegistryHub {
public:
    RegistryHub() = default;
    ~RegistryHub() = default;

    RegistryHub(const RegistryHub&) = delete;
    RegistryHub& operator=(const RegistryHub&) = delete;

    // Built-in registrations are the fallback state restored when a script
    // that shadows the same id is unloaded. Built-in content uses owner 0.
    snt::core::Expected<void> register_builtin_recipe(RecipeDefinition definition);
    snt::core::Expected<void> register_builtin_machine(MachineDefinition definition);
    snt::core::Expected<void> register_builtin_quest(QuestDefinition definition);

    // Script definitions may shadow a built-in definition with the same id.
    // Two different scripts cannot own the same live definition; that would
    // make content load order affect gameplay determinism.
    snt::core::Expected<void> register_script_recipe(ScriptId script_id,
                                                     RecipeDefinition definition);
    snt::core::Expected<void> register_script_machine(ScriptId script_id,
                                                      MachineDefinition definition);
    snt::core::Expected<void> register_script_quest(ScriptId script_id,
                                                    QuestDefinition definition);

    const RecipeDefinition* find_recipe(std::string_view id) const;
    const MachineDefinition* find_machine(std::string_view id) const;
    const QuestDefinition* find_quest(std::string_view id) const;
    std::vector<RecipeDefinition> recipes_for_machine(std::string_view machine_id) const;

    // Event listeners are indirect dispatch contracts. ScriptManager resolves
    // callback_id only after it has selected the current module for script_id.
    snt::core::Expected<void> add_event_listener(EventListener listener);
    std::vector<EventListener> event_listeners(std::string_view event_name) const;

    // StateStore survives reloads but is cleared when the engine session ends.
    // It is namespaced by ScriptId to prevent scripts from corrupting each
    // other's state accidentally.
    snt::core::Expected<void> set_state(ScriptId script_id,
                                        std::string key,
                                        std::string value);
    std::optional<std::string> get_state(ScriptId script_id,
                                         std::string_view key) const;

    // Reload transaction lifecycle. begin_reload removes the script's live
    // definitions and restores built-in fallbacks. A caller must invoke either
    // commit_reload or rollback_reload exactly once afterwards.
    snt::core::Expected<void> begin_reload(ScriptId script_id);
    snt::core::Expected<void> commit_reload(ScriptId script_id);
    snt::core::Expected<void> rollback_reload(ScriptId script_id);

    // Permanently remove a script's content, restoring built-in fallbacks.
    // State is intentionally retained so a later reload can resume it.
    snt::core::Expected<void> unload_script(ScriptId script_id);

    // Clear live content, built-in fallbacks, transactions, and StateStore.
    // ScriptManager calls this only at engine-session boundaries.
    void reset();

private:
    template <typename Definition>
    struct OwnedDefinition {
        ScriptId owner = kBuiltinScriptId;
        Definition definition;
    };

    using RecipeMap = std::map<std::string, OwnedDefinition<RecipeDefinition>, std::less<>>;
    using MachineMap = std::map<std::string, OwnedDefinition<MachineDefinition>, std::less<>>;
    using QuestMap = std::map<std::string, OwnedDefinition<QuestDefinition>, std::less<>>;

    struct ReloadSnapshot {
        RecipeMap recipes;
        MachineMap machines;
        QuestMap quests;
        std::vector<EventListener> event_listeners;
    };

    snt::core::Expected<void> register_recipe(ScriptId owner,
                                              RecipeDefinition definition,
                                              bool builtin);
    snt::core::Expected<void> register_machine(ScriptId owner,
                                               MachineDefinition definition,
                                               bool builtin);
    snt::core::Expected<void> register_quest(ScriptId owner,
                                             QuestDefinition definition,
                                             bool builtin);

    static snt::core::Expected<void> validate(const RecipeDefinition& definition);
    static snt::core::Expected<void> validate(const MachineDefinition& definition);
    static snt::core::Expected<void> validate(const QuestDefinition& definition);
    static snt::core::Expected<void> validate(const EventListener& listener);

    void erase_script_content(ScriptId script_id);
    ReloadSnapshot snapshot_script_content(ScriptId script_id) const;
    void restore_script_content(const ReloadSnapshot& snapshot);
    void sort_event_listeners(std::string_view event_name);

    // `backup_*` stores built-ins. `live_*` is the current definition after
    // optional script overrides. std::map keeps enumeration deterministic.
    RecipeMap backup_recipes_;
    MachineMap backup_machines_;
    QuestMap backup_quests_;
    RecipeMap live_recipes_;
    MachineMap live_machines_;
    QuestMap live_quests_;
    std::map<std::string, std::vector<EventListener>, std::less<>> event_listeners_;
    std::map<ScriptId, std::map<std::string, std::string, std::less<>>> state_store_;
    std::map<ScriptId, ReloadSnapshot> reloads_;
};

}  // namespace snt::script
