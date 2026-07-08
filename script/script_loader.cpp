// Script loader — implementation.

#define SNT_LOG_CHANNEL "script"

#include "script/script_loader.h"

#include <filesystem>
#include <string>

#include "core/log.h"
#include "core/error.h"
#include "core/snt_assert.h"
#include "script/script_context.h"

namespace snt::script {

namespace fs = std::filesystem;

snt::core::Expected<void> ScriptLoader::load_directory(std::string_view dir) {
    SNT_ASSERT_MSG(engine_, "ScriptLoader: engine not set");

    if (!fs::exists(dir)) {
        return snt::core::Error{
            snt::core::ErrorCode::kFileNotFound,
            std::string("Script directory not found: ") + std::string(dir)
        };
    }

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".as") continue;

        std::string stem = entry.path().stem().string();
        std::string path = entry.path().string();

        auto r = load_file(path);
        if (!r) {
            return r.error().with_context(
                std::string("load_directory: ") + path);
        }
    }
    return {};
}

snt::core::Expected<void> ScriptLoader::load_file(std::string_view path) {
    SNT_ASSERT_MSG(engine_, "ScriptLoader: engine not set");

    fs::path p(path);
    std::string name = p.stem().string();

    auto& entry = entries_[name];
    entry.name = name;
    entry.path = std::string(path);

    auto r = entry.module.compile(engine_, name, path);
    if (!r) {
        return r.error().with_context(std::string("load_file: ") + std::string(path));
    }

    // Record mtime for hot-reload detection.
    std::error_code ec;
    auto mtime = fs::last_write_time(p, ec);
    if (!ec) {
        entry.last_mtime = mtime;
    }

    SNT_LOG_INFO("Loaded script '%s' from %s", name.c_str(),
                 entry.path.c_str());
    return {};
}

snt::core::Expected<void> ScriptLoader::load_source(std::string_view name,
                                                     std::string_view source) {
    SNT_ASSERT_MSG(engine_, "ScriptLoader: engine not set");

    auto& entry = entries_[std::string(name)];
    entry.name = std::string(name);
    entry.path.clear();  // no file backing → no hot reload
    entry.last_mtime = {};

    auto r = entry.module.compile_source(engine_, name, source);
    if (!r) {
        return r.error().with_context(std::string("load_source: ") + std::string(name));
    }

    SNT_LOG_DEBUG("Loaded inline script '%s' (%zu bytes)",
                  name.data(), source.size());
    return {};
}

size_t ScriptLoader::reload_if_changed(ScriptContextPool& /*pool*/) {
    size_t reloaded = 0;

    for (auto& [name, entry] : entries_) {
        if (entry.path.empty()) continue;  // inline source, no file

        std::error_code ec;
        auto mtime = fs::last_write_time(entry.path, ec);
        if (ec) continue;

        if (mtime <= entry.last_mtime) continue;

        // File changed → recompile.
        SNT_LOG_INFO("Reloading script '%s' (file changed)", name.c_str());
        auto r = entry.module.compile(engine_, name, entry.path);
        if (r) {
            entry.last_mtime = mtime;
            ++reloaded;
        } else {
            SNT_LOG_ERROR("Failed to reload '%s': %s",
                          name.c_str(), r.error().message().c_str());
        }
    }

    return reloaded;
}

ScriptModule* ScriptLoader::get_module(std::string_view name) {
    auto it = entries_.find(std::string(name));
    if (it == entries_.end()) return nullptr;
    return &it->second.module;
}

void ScriptLoader::unload_all() {
    for (auto& [name, entry] : entries_) {
        entry.module.discard();
    }
    entries_.clear();
}

}  // namespace snt::script
