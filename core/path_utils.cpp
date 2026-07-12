// Runtime path contract implementation.

#define SNT_LOG_CHANNEL "paths"
#include "core/log.h"
#include "core/path_utils.h"

#include <filesystem>
#include <system_error>

namespace snt::core::path_utils {
namespace {
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

}  // namespace snt::core::path_utils

namespace snt::core {

RuntimePathResolver::RuntimePathResolver(RuntimePaths paths)
    : paths_(std::move(paths)) {}

Expected<RuntimePathResolver> RuntimePathResolver::create(RuntimePaths paths) {
    using namespace path_utils;

    auto engine_root = normalize_root(paths.engine_root, "engine_root");
    if (!engine_root) return engine_root.error();
    auto game_root = normalize_root(paths.game_root, "game_root");
    if (!game_root) return game_root.error();
    auto user_root = normalize_root(paths.user_root, "user_root");
    if (!user_root) return user_root.error();

    RuntimePaths normalized_paths{
        .engine_root = std::move(*engine_root),
        .game_root = std::move(*game_root),
        .user_root = std::move(*user_root),
    };

    SNT_LOG_INFO("Runtime path resolver created: engine='%s', game='%s', user='%s'",
                 normalized_paths.engine_root.c_str(),
                 normalized_paths.game_root.c_str(),
                 normalized_paths.user_root.c_str());
    return RuntimePathResolver(std::move(normalized_paths));
}

const RuntimePaths& RuntimePathResolver::roots() const noexcept {
    return paths_;
}

std::string RuntimePathResolver::resolve_engine(std::string_view relative_path) const {
    return path_utils::resolve_from(paths_.engine_root, relative_path);
}

std::string RuntimePathResolver::resolve_game(std::string_view relative_path) const {
    return path_utils::resolve_from(paths_.game_root, relative_path);
}

std::string RuntimePathResolver::resolve_user(std::string_view relative_path) const {
    return path_utils::resolve_from(paths_.user_root, relative_path);
}

}  // namespace snt::core

namespace snt::core::path_utils {

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
