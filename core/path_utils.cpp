// Path utilities — implementation.

#define SNT_LOG_CHANNEL "path"
#include "core/log.h"
#include "core/path_utils.h"

#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#  include <windows.h>  // GetModuleFileNameW
#elif defined(__linux__)
#  include <unistd.h>   // readlink
#  include <limits.h>   // PATH_MAX
#endif

namespace snt::core {
namespace path_utils {

namespace {

// Cached root directory (absolute, native separators). Empty until init()
// succeeds.
std::string g_engine_root;

// ---------------------------------------------------------------------------
// Locate the directory containing the running executable.
// ---------------------------------------------------------------------------
// Returns an empty string on failure. The result is absolute + ends with
// the platform separator so concatenation is straightforward.
std::string executable_dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    std::filesystem::path exe_path(buf);
    std::error_code ec;
    auto dir = std::filesystem::absolute(exe_path.parent_path(), ec);
    if (ec) return {};
    std::string s = dir.string();
    if (!s.empty() && s.back() != '\\' && s.back() != '/') {
        s += "\\";
    }
    return s;
#elif defined(__linux__)
    char buf[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return {};
    buf[len] = '\0';
    std::filesystem::path exe_path(buf);
    std::error_code ec;
    auto dir = std::filesystem::absolute(exe_path.parent_path(), ec);
    if (ec) return {};
    std::string s = dir.string();
    if (!s.empty() && s.back() != '/') {
        s += "/";
    }
    return s;
#else
#  error "path_utils::executable_dir() not implemented for this platform"
#endif
}

// Walk up from `start` looking for a marker that identifies the engine
// root. Markers (any one suffices):
//   - "config/engine.json"   (packaged / dev builds after P2.E)
//   - "engine/CMakeLists.txt" (dev source tree, before any copy step)
// Returns the absolute root path (with trailing separator) on success.
std::string find_root_from(const std::string& start) {
    std::error_code ec;
    auto dir = std::filesystem::absolute(start, ec);
    if (ec) return {};

    // Bounded climb to avoid infinite loops on weird filesystems.
    for (int i = 0; i < 16; ++i) {
        const std::string s = dir.string();
        const std::string sep = std::string(1, std::filesystem::path::preferred_separator);

        // Marker 1: packaged layout — config/engine.json next to root.
        if (std::filesystem::exists(dir / "config" / "engine.json", ec)) {
            std::string root = s;
            if (!root.empty() && root.back() != std::filesystem::path::preferred_separator) {
                root += sep;
            }
            return root;
        }

        // Marker 2: dev source layout — engine/ subdir with CMakeLists.
        if (std::filesystem::exists(dir / "engine" / "CMakeLists.txt", ec)) {
            std::string root = (dir / "engine").string();
            if (!root.empty() && root.back() != std::filesystem::path::preferred_separator) {
                root += sep;
            }
            // In dev layout, runtime paths like "shaders/..." resolve
            // relative to engine/ (where shaders/ lives).
            return root;
        }

        // Stop at filesystem root.
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool init() {
    if (!g_engine_root.empty()) return true;  // already initialized

    const std::string exe_dir = executable_dir();
    if (exe_dir.empty()) {
        SNT_LOG_WARN("path_utils: could not locate executable directory; "
                     "falling back to CWD-relative paths");
        return false;
    }

    std::string root = find_root_from(exe_dir);
    if (root.empty()) {
        SNT_LOG_WARN("path_utils: could not locate engine root from '%s'; "
                     "falling back to CWD-relative paths",
                     exe_dir.c_str());
        return false;
    }

    g_engine_root = root;
    SNT_LOG_INFO("path_utils: engine root = %s", g_engine_root.c_str());
    return true;
}

std::string resolve(std::string_view relative_path) {
    if (g_engine_root.empty()) {
        // init() not called or failed — return input as-is so the engine
        // still runs (with CWD-dependent behavior).
        return std::string(relative_path);
    }
    // Replace forward slashes with native separator for cross-platform
    // config files (which always use '/').
    std::string p(relative_path);
    for (char& c : p) {
        if (c == '/') c = std::filesystem::path::preferred_separator;
    }
    return g_engine_root + p;
}

std::string join(std::string_view a, std::string_view b) {
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    std::string result(a);
    if (result.back() != '/' && result.back() != '\\') {
        result += std::filesystem::path::preferred_separator;
    }
    // Strip a leading separator from b so we don't double up.
    std::string_view b_view = b;
    if (!b_view.empty() && (b_view.front() == '/' || b_view.front() == '\\')) {
        b_view.remove_prefix(1);
    }
    result.append(b_view);
    return result;
}

bool exists(std::string_view path) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(std::string(path)), ec);
}

const std::string& engine_root() {
    return g_engine_root;
}

}  // namespace path_utils
}  // namespace snt::core
