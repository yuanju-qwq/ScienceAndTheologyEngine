// ScriptLoader -- transactional content-module lifetime.
//
// Every file path maps to a stable ScriptId. A reload compiles a unique
// candidate module first, then runs void snt_register() inside the game's
// IScriptContentHost transaction. Only a successful commit discards the old
// module, so failed compilation, entry execution, or callback validation
// cannot leave stale registrations or function pointers behind.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/expected.h"
#include "script/content_host.h"
#include "script/script_module.h"

// AS types are declared as `class` in angelscript.h — see note in
// script_context.h about keeping forward-declaration tags consistent.
class asIScriptEngine;

namespace snt::script {

class ScriptContextPool;

// One loaded script module. `path` is empty only for inline test/editor
// sources; file-backed entries are always normalized absolute paths.
struct ScriptEntry {
    std::string name;
    std::filesystem::path path;
    std::string inline_source;
    ScriptId script_id = kBuiltinScriptId;
    uint64_t generation = 0;
    ScriptModule module;
};

class ScriptLoader {
public:
    ScriptLoader() = default;
    ~ScriptLoader() = default;

    ScriptLoader(const ScriptLoader&) = delete;
    ScriptLoader& operator=(const ScriptLoader&) = delete;

    // The manager owns all three dependencies. The loader only borrows them
    // while ScriptManager is initialized on the main thread.
    void set_runtime(asIScriptEngine* engine,
                     ScriptContextPool* contexts,
                     IScriptContentHost* content_host);

    // Scan `dir` for .as files and load each one as a module. The module
    // name is the file stem (e.g. "foo.as" → "foo"). Returns Err on the
    // first file that fails to compile.
    snt::core::Expected<void> load_directory(std::string_view dir);

    // Load a single .as file. Existing paths are reloaded transactionally.
    snt::core::Expected<void> load_file(std::string_view path);

    // Load an inline source string (no file backing). Used by tests and
    // editor preview; it follows the same registration transaction.
    snt::core::Expected<void> load_source(std::string_view name,
                                          std::string_view source);

    // Reload one watched file, a caller-provided dependency-ordered batch, or
    // all file-backed scripts. A batch compiles every candidate first and
    // then commits all selected ScriptIds together or restores every one.
    snt::core::Expected<void> reload_file(const std::filesystem::path& path);
    snt::core::Expected<void> reload_files_atomically(
        std::span<const std::filesystem::path> paths);
    snt::core::Expected<void> reload_all();

    // Remove a deleted file's module and live registrations. StateStore is
    // deliberately retained by the content host for a future recreation.
    snt::core::Expected<void> unload_file(const std::filesystem::path& path);

    // Look up a loaded module by name. Returns null if not found.
    ScriptModule* get_module(std::string_view name);

    // Event dispatch resolves a host-owned stable ScriptId only at call time,
    // so it always observes the current committed module.
    ScriptModule* get_module(ScriptId script_id);

    // Drop all loaded modules. Called by ScriptManager::shutdown().
    void unload_all();

private:
    snt::core::Expected<void> ensure_runtime() const;
    snt::core::Expected<void> reload_entry(ScriptEntry& entry);
    snt::core::Expected<void> reload_entries_atomically(
        const std::vector<ScriptEntry*>& entries);
    snt::core::Expected<void> compile_candidate(ScriptEntry& entry,
                                                 ScriptModule& candidate);
    snt::core::Expected<void> register_candidate(const ScriptEntry& entry,
                                                  const ScriptModule& candidate);
    snt::core::Expected<void> validate_event_callbacks(const ScriptEntry& entry,
                                                        const ScriptModule& candidate) const;
    snt::core::Expected<std::filesystem::path> normalize_file_path(
        const std::filesystem::path& path) const;
    snt::core::Expected<void> validate_new_entry_name(std::string_view key,
                                                      std::string_view name,
                                                      ScriptId script_id) const;
    static std::string file_key(const std::filesystem::path& path);
    static std::string inline_key(std::string_view name);
    static ScriptId script_id_for_key(std::string_view key);

    asIScriptEngine* engine_ = nullptr;
    ScriptContextPool* contexts_ = nullptr;
    IScriptContentHost* content_host_ = nullptr;
    std::map<std::string, ScriptEntry, std::less<>> entries_;
};

}  // namespace snt::script
