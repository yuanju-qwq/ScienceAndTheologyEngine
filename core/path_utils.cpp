// Runtime path contract implementation.

#define SNT_LOG_CHANNEL "paths"
#include "core/log.h"
#include "core/path_utils.h"

#include <filesystem>
#include <system_error>

namespace snt::core::path_utils {
namespace {

RuntimePaths g_runtime_paths;
bool g_configured = false;

Expected<std::string> normalize_root(std::string_view raw_path, const char* label) {
    if (raw_path.empty()) {
        return Error{ErrorCode::kInvalidArgument,
                     std::string("RuntimePaths requires a non-empty ") + label};
    }

    std::error_code ec;
    const auto absolute = std::filesystem::absolute(std::filesystem::path(raw_path), ec);
    if (ec) {
        return Error{ErrorCode::kInvalidArgument,
                     std::string("Could not normalize ") + label + ": " + ec.message()};
    }
    return absolute.lexically_normal().string();
}

std::string resolve_from(std::string_view root, std::string_view relative_path) {
    const std::filesystem::path path(relative_path);
    if (path.is_absolute() || root.empty()) {
        return path.string();
    }
    return (std::filesystem::path(root) / path).lexically_normal().string();
}

}  // namespace

Expected<void> configure(RuntimePaths paths) {
    auto engine_root = normalize_root(paths.engine_root, "engine_root");
    if (!engine_root) return engine_root.error();
    auto game_root = normalize_root(paths.game_root, "game_root");
    if (!game_root) return game_root.error();
    auto user_root = normalize_root(paths.user_root, "user_root");
    if (!user_root) return user_root.error();

    g_runtime_paths = {
        .engine_root = std::move(*engine_root),
        .game_root = std::move(*game_root),
        .user_root = std::move(*user_root),
    };
    g_configured = true;

    SNT_LOG_INFO("Runtime paths configured: engine='%s', game='%s', user='%s'",
                 g_runtime_paths.engine_root.c_str(),
                 g_runtime_paths.game_root.c_str(),
                 g_runtime_paths.user_root.c_str());
    return {};
}

bool configured() {
    return g_configured;
}

const RuntimePaths& runtime_paths() {
    return g_runtime_paths;
}

std::string resolve_engine(std::string_view relative_path) {
    return resolve_from(g_runtime_paths.engine_root, relative_path);
}

std::string resolve_game(std::string_view relative_path) {
    return resolve_from(g_runtime_paths.game_root, relative_path);
}

std::string resolve_user(std::string_view relative_path) {
    return resolve_from(g_runtime_paths.user_root, relative_path);
}

std::string join(std::string_view a, std::string_view b) {
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    return (std::filesystem::path(a) / std::filesystem::path(b)).string();
}

bool exists(std::string_view path) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(path), ec);
}

}  // namespace snt::core::path_utils
