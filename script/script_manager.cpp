// Script manager — implementation.

#define SNT_LOG_CHANNEL "script"

#include "script/script_manager.h"

#include "core/error.h"
#include "core/log.h"
#include "script/script_api.h"

namespace snt::script {

ScriptManager& ScriptManager::instance() {
    static ScriptManager inst;
    return inst;
}

snt::core::Expected<void> ScriptManager::init() {
    if (initialized_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ScriptManager is already initialized"};
    }
    SNT_LOG_INFO("Initializing ScriptManager...");

    registry_hub_.reset();
    const auto fail = [this](snt::core::Error error) -> snt::core::Expected<void> {
        loader_.set_runtime(nullptr, nullptr, nullptr);
        watcher_.reset();
        contexts_.shutdown();
        engine_.shutdown();
        registry_hub_.reset();
        return error;
    };

    auto r = engine_.init();
    if (!r) {
        snt::core::Error error = r.error();
        error.with_context("ScriptManager::init(engine)");
        return fail(std::move(error));
    }

    r = engine_.register_core_types();
    if (!r) {
        snt::core::Error error = r.error();
        error.with_context("ScriptManager::init(core types)");
        return fail(std::move(error));
    }
    r = register_gameplay_script_api(engine_.raw());
    if (!r) {
        snt::core::Error error = r.error();
        error.with_context("ScriptManager::init(gameplay API)");
        return fail(std::move(error));
    }

    r = contexts_.init(engine_.raw(), 1);
    if (!r) {
        snt::core::Error error = r.error();
        error.with_context("ScriptManager::init(context pool)");
        return fail(std::move(error));
    }

    loader_.set_runtime(engine_.raw(), &contexts_, &registry_hub_);
    watcher_ = create_polling_file_watcher();
    if (!watcher_) {
        return fail(snt::core::Error{snt::core::ErrorCode::kScriptEngineInitFailed,
                                     "Could not create P7 file watcher"});
    }

    initialized_ = true;
    SNT_LOG_INFO("ScriptManager initialized");
    return {};
}

void ScriptManager::update(float /*dt*/) {
    if (!initialized_) return;

    if (watcher_ && watcher_->running()) {
        const std::vector<FileChange> changes = watcher_->drain_changes();
        size_t applied = 0;
        size_t failed = 0;
        for (const FileChange& change : changes) {
            const auto result = change.kind == FileChangeKind::Removed
                ? loader_.unload_file(change.path)
                : loader_.reload_file(change.path);
            if (!result) {
                ++failed;
                SNT_LOG_ERROR("P7 script change rejected for %s: %s",
                              change.path.string().c_str(), result.error().format().c_str());
            } else {
                ++applied;
            }
        }
        if (!changes.empty()) {
            SNT_LOG_INFO("P7 file watcher processed %zu change(s): %zu applied, %zu rolled back",
                         changes.size(), applied, failed);
        }
    }

    contexts_.gc_step();
}

snt::core::Expected<void> ScriptManager::watch_directory(const std::filesystem::path& root) {
    if (!initialized_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ScriptManager is not initialized"};
    }
    if (watcher_->running()) {
        watcher_->stop();
    }
    if (auto loaded = loader_.load_directory(root.string()); !loaded) {
        return loaded.error();
    }
    if (auto started = watcher_->start(root, {".as"}); !started) {
        return started.error();
    }
    return {};
}

snt::core::Expected<void> ScriptManager::reload_all() {
    if (!initialized_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "ScriptManager is not initialized"};
    }
    return loader_.reload_all();
}

snt::core::Expected<void> ScriptManager::execute_command(std::string_view command) {
    const size_t begin = command.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Empty engine command"};
    }
    const size_t end = command.find_last_not_of(" \t\r\n");
    const std::string_view normalized = command.substr(begin, end - begin + 1);
    if (normalized != "/snt reload") {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Unknown script command: " + std::string(normalized)};
    }
    return reload_all();
}

void ScriptManager::shutdown() {
    if (!initialized_) return;
    SNT_LOG_INFO("Shutting down ScriptManager...");
    if (watcher_) watcher_->stop();
    loader_.unload_all();
    registry_hub_.reset();
    contexts_.shutdown();
    engine_.shutdown();
    loader_.set_runtime(nullptr, nullptr, nullptr);
    watcher_.reset();
    initialized_ = false;
    SNT_LOG_INFO("ScriptManager shut down");
}

}  // namespace snt::script
