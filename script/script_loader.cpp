// Script loader — implementation.

#define SNT_LOG_CHANNEL "script"

#include "script/script_loader.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/hash.h"
#include "core/log.h"
#include "script/script_api.h"
#include "script/script_context.h"

namespace snt::script {

namespace fs = std::filesystem;

void ScriptLoader::set_runtime(asIScriptEngine* engine,
                               ScriptContextPool* contexts,
                               RegistryHub* registries) {
    engine_ = engine;
    contexts_ = contexts;
    registries_ = registries;
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
    SNT_LOG_INFO("Loaded gameplay script '%s' (%llu)", name.c_str(),
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
    SNT_LOG_DEBUG("Loaded inline gameplay script '%.*s'", static_cast<int>(name.size()), name.data());
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

snt::core::Expected<void> ScriptLoader::reload_all() {
    if (auto runtime = ensure_runtime(); !runtime) {
        return runtime.error();
    }

    size_t reloaded = 0;
    for (auto& [key, entry] : entries_) {
        (void)key;
        if (entry.path.empty()) continue;
        if (auto result = reload_entry(entry); !result) {
            snt::core::Error error = result.error();
            error.with_context("ScriptLoader::reload_all(" + entry.path.string() + ")");
            return error;
        }
        ++reloaded;
    }
    SNT_LOG_INFO("Reloaded %zu gameplay script module(s)", reloaded);
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
    if (auto result = registries_->unload_script(entry->second.script_id); !result) {
        return result.error();
    }
    entry->second.module.discard();
    entries_.erase(entry);
    SNT_LOG_INFO("Unloaded removed gameplay script: %s", normalized->string().c_str());
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
    for (auto& [key, entry] : entries_) {
        (void)key;
        if (registries_ && entry.script_id != kBuiltinScriptId) {
            if (auto result = registries_->unload_script(entry.script_id); !result) {
                SNT_LOG_ERROR("Failed to unload script %llu: %s",
                              static_cast<unsigned long long>(entry.script_id),
                              result.error().format().c_str());
            }
        }
        entry.module.discard();
    }
    entries_.clear();
}

snt::core::Expected<void> ScriptLoader::ensure_runtime() const {
    if (engine_ && contexts_ && registries_) {
        return {};
    }
    return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                            "ScriptLoader runtime dependencies are not initialized"};
}

snt::core::Expected<void> ScriptLoader::reload_entry(ScriptEntry& entry) {
    std::string module_name = "p7_" + std::to_string(entry.script_id) + "_" +
                              std::to_string(++entry.generation);
    ScriptModule candidate;
    snt::core::Expected<void> compiled = entry.path.empty()
        ? candidate.compile_source(engine_, module_name, entry.inline_source)
        : candidate.compile(engine_, module_name, entry.path.string());
    if (!compiled) {
        return compiled.error();
    }
    return activate_candidate(entry, std::move(candidate));
}

snt::core::Expected<void> ScriptLoader::activate_candidate(ScriptEntry& entry,
                                                             ScriptModule&& candidate) {
    if (auto begin = registries_->begin_reload(entry.script_id); !begin) {
        return begin.error();
    }

    const auto rollback = [this, &entry]() {
        if (auto result = registries_->rollback_reload(entry.script_id); !result) {
            SNT_LOG_ERROR("Failed to roll back script %llu: %s",
                          static_cast<unsigned long long>(entry.script_id),
                          result.error().format().c_str());
        }
    };

    const auto registered = [&]() -> snt::core::Expected<void> {
        ScriptRegistrationScope scope(*registries_, entry.script_id);
        return candidate.call_void(*contexts_, "void snt_register()");
    }();
    if (!registered) {
        rollback();
        return registered.error();
    }

    if (auto callbacks = validate_event_callbacks(entry, candidate); !callbacks) {
        rollback();
        return callbacks.error();
    }

    if (auto committed = registries_->commit_reload(entry.script_id); !committed) {
        rollback();
        return committed.error();
    }

    entry.module = std::move(candidate);
    SNT_LOG_INFO("Committed gameplay script '%s' (generation %llu)",
                 entry.name.c_str(), static_cast<unsigned long long>(entry.generation));
    return {};
}

snt::core::Expected<void> ScriptLoader::validate_event_callbacks(
    const ScriptEntry& entry, const ScriptModule& candidate) const {
    for (const EventListener& listener : registries_->event_listeners_for_script(entry.script_id)) {
        const std::string declaration = "void " + listener.callback_id + "()";
        if (!candidate.get_function(declaration)) {
            return snt::core::Error{
                snt::core::ErrorCode::kScriptModuleNotFound,
                "Event callback '" + listener.callback_id + "' is not declared as " + declaration};
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
                                    "Duplicate gameplay script module name: " + std::string(name)};
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
