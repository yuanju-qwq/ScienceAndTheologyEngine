// Script manager: top-level façade owning ScriptEngine, ScriptContextPool,
// and ScriptLoader.
//
// Lifecycle:
//   - init()    — create AS engine + context pool + register core types
//   - update()  — poll for .as file changes; run one GC step
//   - shutdown() — release everything in reverse order
//
// ScriptManager is owned by Runtime. A game injects one IScriptContentHost
// before init() to own content definitions and native script bindings, while
// tests construct isolated local instances with a test host.

#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "core/expected.h"
#include "script/content_host.h"
#include "script/file_watcher.h"
#include "script/script_engine.h"
#include "script/script_context.h"
#include "script/script_loader.h"

namespace snt::script {

class ScriptManager;

// A game may translate a changed file into a dependency-aware batch. Returning
// true means the change was handled; false delegates to ScriptManager's
// generic single-file behavior for unmanaged scripts.
class IScriptFileChangeHandler {
public:
    virtual ~IScriptFileChangeHandler() = default;
    virtual snt::core::Expected<bool> handle_script_file_change(
        ScriptManager& scripts, const FileChange& change) = 0;
};

class ScriptManager {
public:
    ScriptManager() = default;
    ~ScriptManager() { shutdown(); }

    // Configure the game-owned content host before init(). ScriptManager only
    // borrows it for one initialized session and clears the reference during
    // shutdown or initialization failure.
    snt::core::Expected<void> set_content_host(IScriptContentHost& content_host);

    // Install a game-owned file-change policy. The handler is borrowed and
    // must outlive this manager or be cleared before its owner is destroyed.
    void set_file_change_handler(IScriptFileChangeHandler* handler) noexcept {
        file_change_handler_ = handler;
    }

    // Create the AS engine, context pool, and register core types.
    snt::core::Expected<void> init();

    // Per-frame update: check for .as file changes and reload if needed.
    // Also runs one incremental GC step to keep script memory bounded.
    // `dt` is the frame delta in seconds (currently unused, reserved
    // for future throttling of reload checks).
    void update(float dt);

    // Load every content module under `root`, then start the file watcher.
    // The watcher enqueues changes only; update() consumes them on
    // the main thread and performs per-script transactions.
    snt::core::Expected<void> watch_directory(const std::filesystem::path& root);

    // Explicit reload entry point used by the command layer. It follows the
    // same compile/register/commit or rollback flow as file changes.
    snt::core::Expected<void> reload_all();
    snt::core::Expected<void> reload_files(
        std::span<const std::filesystem::path> paths);

    // Invoke a no-argument callback from the currently committed generation
    // of one content module. Client-only extension hosts use this for
    // deliberate UI actions; callers stay on the script main thread.
    snt::core::Expected<void> invoke_callback(ScriptId script_id,
                                              std::string_view callback_id);

    // Release the AS engine and all modules.
    void shutdown();

    // ---- Generic script API (used by engine hosts) ----

    // Load all .as content files under `dir` recursively.
    snt::core::Expected<void> load_directory(std::string_view dir) {
        return loader_.load_directory(dir);
    }

    // Load a single .as file.
    snt::core::Expected<void> load_file(std::string_view path) {
        return loader_.load_file(path);
    }

    // Load an inline source string (no file backing).
    snt::core::Expected<void> load_source(std::string_view name,
                                          std::string_view source) {
        return loader_.load_source(name, source);
    }

    // Look up a loaded module by name.
    ScriptModule* get_module(std::string_view name) {
        return loader_.get_module(name);
    }
    ScriptModule* get_module(ScriptId script_id) {
        return loader_.get_module(script_id);
    }

    ScriptEngine&       engine()  { return engine_; }
    ScriptContextPool&  contexts() { return contexts_; }
    ScriptLoader&       loader()  { return loader_; }

private:
    ScriptManager(const ScriptManager&) = delete;
    ScriptManager& operator=(const ScriptManager&) = delete;

    ScriptEngine      engine_;
    ScriptContextPool contexts_;
    ScriptLoader      loader_;
    IScriptContentHost* content_host_ = nullptr;
    IScriptFileChangeHandler* file_change_handler_ = nullptr;
    std::unique_ptr<FileWatcher> watcher_;
    bool initialized_ = false;
};

}  // namespace snt::script
