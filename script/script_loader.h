// Script loader: discovers, loads, and hot-reloads .as script modules.
//
// Maintains a map of {module_name → ScriptModule}. On update(), each
// loaded module's source file mtime is checked; if the file changed
// since the last load, the module is recompiled in place.
//
// Reload strategy: simple full reload (every module is recompiled when
// any of them changes). This avoids the complexity of dependency-graph
// topological sorting for the common case of <10 small script files.

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "core/expected.h"
#include "script/script_module.h"

// AS types are declared as `class` in angelscript.h — see note in
// script_context.h about keeping forward-declaration tags consistent.
class asIScriptEngine;

namespace snt::script {

class ScriptContextPool;

// One loaded script module + its source file + last-seen mtime.
struct ScriptEntry {
    std::string name;        // module name (usually file stem)
    std::string path;        // absolute path to .as file
    std::filesystem::file_time_type last_mtime{};
    ScriptModule module;
};

class ScriptLoader {
public:
    ScriptLoader() = default;
    ~ScriptLoader() = default;

    ScriptLoader(const ScriptLoader&) = delete;
    ScriptLoader& operator=(const ScriptLoader&) = delete;

    // Remember the engine to use for future compiles. Does not take ownership.
    void set_engine(asIScriptEngine* engine) { engine_ = engine; }

    // Scan `dir` for .as files and load each one as a module. The module
    // name is the file stem (e.g. "foo.as" → "foo"). Returns Err on the
    // first file that fails to compile.
    snt::core::Expected<void> load_directory(std::string_view dir);

    // Load or reload a single .as file. Returns Err on compile failure.
    snt::core::Expected<void> load_file(std::string_view path);

    // Reload an inline source string (no file backing). Used by tests
    // and editor preview. The module is named `name`.
    snt::core::Expected<void> load_source(std::string_view name,
                                          std::string_view source);

    // Check all file-backed modules' mtimes; reload any that changed.
    // Call this once per frame (or throttled to ~1 Hz).
    // Returns the number of modules reloaded.
    size_t reload_if_changed(ScriptContextPool& pool);

    // Look up a loaded module by name. Returns null if not found.
    ScriptModule* get_module(std::string_view name);

    // Drop all loaded modules. Called by ScriptManager::shutdown().
    void unload_all();

private:
    asIScriptEngine* engine_ = nullptr;
    std::unordered_map<std::string, ScriptEntry> entries_;
};

}  // namespace snt::script
