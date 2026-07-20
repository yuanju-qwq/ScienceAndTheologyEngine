// Script loader — implementation.

#define SNT_LOG_CHANNEL "script"

#include "script/script_loader.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/hash.h"
#include "core/log.h"
#include "script/script_context.h"

namespace snt::script {

namespace fs = std::filesystem;

void ScriptLoader::set_runtime(asIScriptEngine* engine,
                               ScriptContextPool* contexts,
                               IScriptContentHost* content_host) {
    engine_ = engine;
    contexts_ = contexts;
    content_host_ = content_host;
}

snt::core::Expected<void> ScriptLoader::load_directory(std::string_view dir) {
    if (auto runtime = ensure_runtime(); !runtime) {
        return runtime.error();
    }

    std::error_code ec;
    const fs::path root = fs::absolute(fs::path(dir), ec);
    if (ec || !fs::is_directory(root, ec)) {
        return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                "Script directory not found: " + std::string(dir)};
    }

    std::vector<fs::path> files;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        const fs::directory_entry entry = *it;
        it.increment(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code file_ec;
        if (entry.is_regular_file(file_ec) && !file_ec && entry.path().extension() == ".as") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end(), [](const fs::path& lhs, const fs::path& rhs) {
        return lhs.generic_string() < rhs.generic_string();
    });
    for (const fs::path& file : files) {
        if (auto result = load_file(file.string()); !result) {
            snt::core::Error error = result.error();
            error.with_context("ScriptLoader::load_directory(" + file.string() + ")");
            return error;
        }
    }
    return {};
}

snt::core::Expected<void> ScriptLoader::load_file(std::string_view path) {
    if (auto runtime = ensure_runtime(); !runtime) {
        return runtime.error();
    }

    auto normalized = normalize_file_path(fs::path(path));
    if (!normalized) return normalized.error();
    std::error_code ec;
    if (!fs::is_regular_file(*normalized, ec) || ec) {
        return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                "Script file not found: " + normalized->string()};
    }

    const std::string key = file_key(*normalized);
    if (auto existing = entries_.find(key); existing != entries_.end()) {
        return reload_entry(existing->second);
    }

    const std::string name = normalized->stem().string();
    const ScriptId script_id = script_id_for_key(key);
    if (auto valid = validate_new_entry_name(key, name, script_id); !valid) {
        return valid.error();
    }

    ScriptEntry entry;
    entry.name = name;
    entry.path = *normalized;
    entry.script_id = script_id;
    if (auto result = reload_entry(entry); !result) {
        snt::core::Error error = result.error();
        error.with_context("ScriptLoader::load_file(" + normalized->string() + ")");
        return error;
    }

    entries_.emplace(key, std::move(entry));
    SNT_LOG_INFO("Loaded content script '%s' (%llu)", name.c_str(),
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> ScriptLoader::load_source(std::string_view name,
                                                     std::string_view source) {
    if (auto runtime = ensure_runtime(); !runtime) {
        return runtime.error();
    }
    if (name.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Inline script name must not be empty"};
    }

    const std::string key = inline_key(name);
    if (auto existing = entries_.find(key); existing != entries_.end()) {
        std::string previous_source = std::move(existing->second.inline_source);
        existing->second.inline_source = std::string(source);
        auto result = reload_entry(existing->second);
        if (!result) {
            existing->second.inline_source = std::move(previous_source);
            return result.error();
        }
        return {};
    }

    const ScriptId script_id = script_id_for_key(key);
    if (auto valid = validate_new_entry_name(key, name, script_id); !valid) {
        return valid.error();
    }

    ScriptEntry entry;
    entry.name = std::string(name);
    entry.inline_source = std::string(source);
    entry.script_id = script_id;
    if (auto result = reload_entry(entry); !result) {
        snt::core::Error error = result.error();
        error.with_context("ScriptLoader::load_source(" + std::string(name) + ")");
        return error;
    }

    entries_.emplace(key, std::move(entry));
    SNT_LOG_DEBUG("Loaded inline content script '%.*s'", static_cast<int>(name.size()), name.data());
    return {};
}

snt::core::Expected<void> ScriptLoader::reload_file(const fs::path& path) {
    if (auto runtime = ensure_runtime(); !runtime) {
        return runtime.error();
    }
    auto normalized = normalize_file_path(path);
    if (!normalized) return normalized.error();
    const std::string key = file_key(*normalized);
    auto entry = entries_.find(key);
    if (entry == entries_.end()) {
        return load_file(normalized->string());
    }
    return reload_entry(entry->second);
}

snt::core::Expected<void> ScriptLoader::reload_files_atomically(
    std::span<const fs::path> paths) {
    if (auto runtime = ensure_runtime(); !runtime) {
        return runtime.error();
    }
    if (paths.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Script reload batch must contain at least one file"};
    }

    std::set<std::string, std::less<>> seen_keys;
    std::vector<ScriptEntry*> selected;
    selected.reserve(paths.size());
    for (const fs::path& path : paths) {
        auto normalized = normalize_file_path(path);
        if (!normalized) return normalized.error();
        const std::string key = file_key(*normalized);
        const auto entry = entries_.find(key);
        if (entry == entries_.end()) {
            return snt::core::Error{snt::core::ErrorCode::kScriptModuleNotFound,
                                    "Script reload batch contains an unloaded file: " +
                                        normalized->string()};
        }
        if (!seen_keys.insert(key).second) continue;
        selected.push_back(&entry->second);
    }
    return reload_entries_atomically(selected);
}

snt::core::Expected<void> ScriptLoader::reload_all() {
    if (auto runtime = ensure_runtime(); !runtime) {
        return runtime.error();
    }

    std::vector<ScriptEntry*> selected;
    selected.reserve(entries_.size());
    for (auto& [key, entry] : entries_) {
        (void)key;
        if (entry.path.empty()) continue;
        selected.push_back(&entry);
    }
    if (selected.empty()) return {};
    if (auto result = reload_entries_atomically(selected); !result) {
        auto error = result.error();
        error.with_context("ScriptLoader::reload_all");
        return error;
    }
    SNT_LOG_INFO("Reloaded %zu content script module(s) atomically", selected.size());
    return {};
}

snt::core::Expected<void> ScriptLoader::unload_file(const fs::path& path) {
    if (auto runtime = ensure_runtime(); !runtime) {
        return runtime.error();
    }
    auto normalized = normalize_file_path(path);
    if (!normalized) return normalized.error();
    const auto entry = entries_.find(file_key(*normalized));
    if (entry == entries_.end()) {
        return {};
    }
    if (auto result = content_host_->unload_script(entry->second.script_id); !result) {
        return result.error();
    }
    entry->second.module.discard();
    entries_.erase(entry);
    SNT_LOG_INFO("Unloaded removed content script: %s", normalized->string().c_str());
    return {};
}

ScriptModule* ScriptLoader::get_module(std::string_view name) {
    for (auto& [key, entry] : entries_) {
        (void)key;
        if (entry.name == name) {
            return &entry.module;
        }
    }
    return nullptr;
}

ScriptModule* ScriptLoader::get_module(ScriptId script_id) {
    for (auto& [key, entry] : entries_) {
        (void)key;
        if (entry.script_id == script_id) {
            return &entry.module;
        }
    }
    return nullptr;
}

void ScriptLoader::unload_all() {
    // Directory loading is stable-path ascending, so later modules may
    // reference content supplied by earlier modules. Tear down in reverse
    // order to remove dependents before their material/item providers.
    for (auto entry = entries_.rbegin(); entry != entries_.rend(); ++entry) {
        ScriptEntry& loaded = entry->second;
        if (content_host_ && loaded.script_id != kBuiltinScriptId) {
            if (auto result = content_host_->unload_script(loaded.script_id); !result) {
                SNT_LOG_ERROR("Failed to unload script %llu: %s",
                              static_cast<unsigned long long>(loaded.script_id),
                              result.error().format().c_str());
            }
        }
        loaded.module.discard();
    }
    entries_.clear();
}

snt::core::Expected<void> ScriptLoader::ensure_runtime() const {
    if (engine_ && contexts_ && content_host_) {
        return {};
    }
    return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                            "ScriptLoader runtime dependencies are not initialized"};
}

snt::core::Expected<void> ScriptLoader::reload_entry(ScriptEntry& entry) {
    const std::vector<ScriptEntry*> entries{&entry};
    return reload_entries_atomically(entries);
}

snt::core::Expected<void> ScriptLoader::reload_entries_atomically(
    const std::vector<ScriptEntry*>& entries) {
    if (auto runtime = ensure_runtime(); !runtime) {
        return runtime.error();
    }
    if (entries.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Script reload batch must contain at least one entry"};
    }

    struct Candidate {
        ScriptEntry* entry = nullptr;
        ScriptModule module;
    };

    std::set<ScriptId> seen_ids;
    std::vector<Candidate> candidates;
    candidates.reserve(entries.size());
    std::vector<ScriptId> script_ids;
    script_ids.reserve(entries.size());
    for (ScriptEntry* const entry : entries) {
        if (!entry) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "Script reload batch contains a null entry"};
        }
        if (!seen_ids.insert(entry->script_id).second) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "Script reload batch contains a duplicate ScriptId"};
        }
        candidates.emplace_back();
        Candidate& candidate = candidates.back();
        candidate.entry = entry;
        if (auto compiled = compile_candidate(*entry, candidate.module); !compiled) {
            auto error = compiled.error();
            error.with_context("ScriptLoader::compile(" + entry->name + ")");
            return error;
        }
        script_ids.push_back(entry->script_id);
    }

    const std::span<const ScriptId> ids(script_ids.data(), script_ids.size());
    if (auto begin = content_host_->begin_reload_batch(ids); !begin) {
        return begin.error();
    }
    const auto rollback = [this, ids]() {
        if (auto result = content_host_->rollback_reload_batch(ids); !result) {
            SNT_LOG_ERROR("Failed to roll back content script batch: %s",
                          result.error().format().c_str());
        }
    };

    for (const Candidate& candidate : candidates) {
        if (auto registered = register_candidate(*candidate.entry, candidate.module); !registered) {
            rollback();
            auto error = registered.error();
            error.with_context("ScriptLoader::register(" + candidate.entry->name + ")");
            return error;
        }
    }
    if (auto committed = content_host_->commit_reload_batch(ids); !committed) {
        rollback();
        return committed.error();
    }

    for (Candidate& candidate : candidates) {
        candidate.entry->module = std::move(candidate.module);
    }
    SNT_LOG_INFO("Committed content script batch with %zu module(s)", candidates.size());
    return {};
}

snt::core::Expected<void> ScriptLoader::compile_candidate(ScriptEntry& entry,
                                                           ScriptModule& candidate) {
    std::string module_name = "p7_" + std::to_string(entry.script_id) + "_" +
                              std::to_string(++entry.generation);
    snt::core::Expected<void> compiled = entry.path.empty()
        ? candidate.compile_source(engine_, module_name, entry.inline_source)
        : candidate.compile(engine_, module_name, entry.path.string());
    return compiled;
}

snt::core::Expected<void> ScriptLoader::register_candidate(
    const ScriptEntry& entry, const ScriptModule& candidate) {
    auto registration_scope = content_host_->begin_registration(entry.script_id);
    if (!registration_scope) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Script content host returned a null registration scope"};
    }
    if (auto registered = candidate.call_void(*contexts_, "void snt_register()"); !registered) {
        return registered.error();
    }
    return validate_event_callbacks(entry, candidate);
}

snt::core::Expected<void> ScriptLoader::validate_event_callbacks(
    const ScriptEntry& entry, const ScriptModule& candidate) const {
    for (const std::string& callback_id : content_host_->callback_ids_for_script(entry.script_id)) {
        const std::string declaration = "void " + callback_id + "()";
        if (!candidate.get_function(declaration)) {
            return snt::core::Error{
                snt::core::ErrorCode::kScriptModuleNotFound,
                "Event callback '" + callback_id + "' is not declared as " + declaration};
        }
    }
    return {};
}

snt::core::Expected<fs::path> ScriptLoader::normalize_file_path(const fs::path& path) const {
    if (path.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Script file path must not be empty"};
    }
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Cannot normalize script path: " + path.string()};
    }
    return absolute.lexically_normal();
}

snt::core::Expected<void> ScriptLoader::validate_new_entry_name(
    std::string_view key, std::string_view name, ScriptId script_id) const {
    if (name.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Script module name must not be empty"};
    }
    if (script_id == kBuiltinScriptId) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ScriptId hash collided with the built-in owner"};
    }
    for (const auto& [existing_key, entry] : entries_) {
        if (existing_key == key) continue;
        if (entry.name == name) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "Duplicate content script module name: " + std::string(name)};
        }
        if (entry.script_id == script_id) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "Stable ScriptId collision for: " + std::string(key)};
        }
    }
    return {};
}

std::string ScriptLoader::file_key(const fs::path& path) {
    return path.generic_string();
}

std::string ScriptLoader::inline_key(std::string_view name) {
    return "inline:" + std::string(name);
}

ScriptId ScriptLoader::script_id_for_key(std::string_view key) {
    return snt::core::hash_string(key);
}

}  // namespace snt::script
